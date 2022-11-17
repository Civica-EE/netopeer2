/**
 * @file rc.js
 *
 * @brief RestConf utilities.
 *
 * @author Alexandru Ponoviciu <alexandru.panoviciu@civica.co.uk>
 *
 * @copyright
 * Copyright (c) 2022 Civica NI Ltd
 *
 * This source code is licensed under BSD 3-Clause License (the "License").
 * You may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://opensource.org/licenses/BSD-3-Clause
 */


/**
 * Abstracts interaction with RESTCONF capable network nodes.
 *
 * The methods prefixed with 'fetch' are asynchronous and return a promise.
 */
class RcNode {
    constructor (uri) {
        this.uri = uri
    }

    /// Begins downloading the specified datastore, as Json
    fetchDs (dsName = null) {
        if (dsName !== null) {
            if (dsName.match('(.*):(.*)') === null) {
                dsName = `ietf-datastores:${dsName}`
            }
            return $.getJSON(`${this.uri}/ds/${dsName}`)
        } else {
            return $.getJSON(`${this.uri}/data`)
        }
    }

    get defaultDsName () {
        return 'operational'
    }

    fetchSchemaList () {
        return $.getJSON(`${this.uri}/data/ietf-yang-library:yang-library/module-set`).
            then(function (data) {
                return data['ietf-netconf:data']['ietf-yang-library:yang-library']['module-set'][0]['module']
            })
    }

    fetchDsList () {
        return $.getJSON(`${this.uri}/data/ietf-yang-library:yang-library/datastore`).
            then(function (data) {
                return data['ietf-netconf:data']['ietf-yang-library:yang-library']['datastore'].
                    map(e => e.name)
            })
        
    }

    // Begins downloading modName's schema, as YIN.
    fetchSchemaYin (modName) {
        return jQuery.post({'url' : '/cgi-bin/restconf/operations/urn:ietf:params:xml:ns:yang:ietf-netconf-monitoring:get-schema',
                            'data' : `<input xmlns="urn:ietf:params:xml:ns:yang:ietf-netconf-monitoring"><identifier>${modName}</identifier><format>yin</format></input>`,
                            'contentType' : 'application/yang-data+xml',
                            'dataFilter' : function (data, type) {
                                // The yin XML comes down as the inner text of a
                                // 'data' XML envelope element, and it's escaped - we
                                // need to parse the envelope first to get to the
                                // unescaped text of it, which becomes the 'real' xml
                                // to be returned.
                                
                                //say(data);
                                let envelope = $.parseXML(data);
                                let yinText = envelope.childNodes[0].childNodes[0].data;
                                return yinText;
                            },
                            'accepts' : { // Needed to get the dataFilter to kick in
                                'xml' : "application/yang-data+xml"
                            },
                            'dataType' : 'xml'})
    }
}


// Turn each module's yin schema into a json tree with all 'uses' etc
// references resolved to the actual containers (so the tree mirrors the data
// trees). Eeach level's 'yin' attribute points to the originating XML yin
// node.
function resolveSchemas (mods) {
    function getMod (yinNode) {
        var mod = yinNode
        while (mod.nodeName != 'module')
            mod = mod.parentNode
        return mod
    }
    function resolveGrouping (uses, path) {
        var gname = uses.attributes['name'].value
        var ns = null
        var $mod = $(getMod(uses))

        const m = gname.match('(.*):(.*)')
        if (m !== null) {
            [,ns, gname] = m
            if ($mod.children(`prefix[value=${ns}]`).length == 0) { // not us, check imports
                let mname = $mod.children(`import:has(prefix[value=${ns}])`).attr('module')
                $mod = $(mods[mname].yin).children('module')
            }
        }

        return resolveNode($mod.find(`grouping[name=${gname}]`), path + `/<${gname}>`)
    }
    
    function resolveNode ($level, path) {
        var node = {'yin' : [$level[0]]}
        
        path += '/' + $level.attr('name')
        //console.log(`resolveNode: ${path}`)
        
        $level.children('container,list,leaf,leaf-list,choice').each(function (i, child) {
            let $child = $(child)
            node[$child.attr('name')] = resolveNode($child, path)
        })

        $level.children('uses').each(function (i, uses) {
            var gnode = resolveGrouping(uses, path)
            for (var c in gnode) {
                if (c != 'yin') { // Don't ovewrite the yin attribute, but
                                  // save in the members we've inherited as
                                  // 'gin' (like yin, but more fun :).
                    node[c] = gnode[c]
                    node[c].gin = gnode.yin
                }
            }
        })

        return node
    }

    function resolveModule (mod) {
        let $m = $(mod.yin).children('module')
        mod.schema = resolveNode($m, '')
    }

    for (var modName in mods) {
        resolveModule(mods[modName])
    }
}


// Expands a JSON datastore tree into a format suitable for a presentation layer:
//
// Every node of the form
//
// { 'myContainer' : { 'subcontainer1' : { ... }, 'leaf1' : 'some_string', ...} }
//
// is transfomed into:
//
// { 'myContainer' : { 'schema' : <json schema node for myContainer>,
//                     'value' : {
//                         'subcontainer1' : {
//                              'schema' : <json schema node for subcontainer1>,
//                              'value' : { ... }
//                         },
//                         'leaf1' : {
//                              'schema' : <json schema node for leaf1>,
//                              'value'  : 'some_string',
//                         }
//                   }
// }
//
function parseDs(json, mods) {

    // Returns the schema for the yang sub-statement with key `key` within the
    // statement described by `schema`
    function getSchema (schema, key) {
        const m = key.match('(.*):(.*)')
        if (m !== null) {
            var [, ns, key] = m
            //console.log(m, keys(mods), mods[m[1]], mods[m[1]][m[2]])
            let mod = mods[ns]
            if (mod === undefined)
                return undefined
            schema = mod.schema
        }

        if (schema === undefined)
        {
            console.warn(`No schema for ${key}`)
            return undefined
        }
        
        return schema[key]
    }

    function parseNode (key, value, schema) {
        const node = {'value' : typeof value === 'object' ? parseChildren(value, schema) :
                                                            value
                     };

        if (schema !== undefined) {
            let description = $(schema.yin).children('description').children('text').text()
            if (description != "") {
                node.doc = description
            }
        }

        return node
    }

    function parseChildren (children, schema) {
        let r = {}
        for (var key in children) {
            r[key] = parseNode(key, children[key],
                               children instanceof Array ? schema : getSchema(schema, key))
        }
        return r
    }

    return parseChildren(json, mods)
}



