/**
 * @file jtree.js
 *
 * @brief Tree viewer and other assorted widgets for a RestConf GUI.
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

var _escapist = document.createElement('textarea');
function escapeHTML(html) {
    _escapist.textContent = html;
    return _escapist.innerHTML;
}

function unescapeHTML(html) {
    _escapist.innerHTML = html;
    return _escapist.textContent;
}

function keys (collection) {
    var keys = []
    for (var k in collection) { keys.push(k) }
    return keys
}


function values (collection) {
    var values = []
    for (var k in collection) { values.push(collection[k]) }
    return values
}



/**
 * Chuck x into the console widget.
 */
function say (x) {
    let $d = $('#debug').append(escapeHTML(x) + '\n').parent()
    $d.scrollTop($d.scrollTop() + 1000)
    return x
}

/**
 * Creates a basic tree view based on a json object - each sub-level is a branch.
 */
function jsonViewer(json, collapsible=false) {
    function createItem(key, value, type)
    {
        return `<div class="jtree__item">
                    <div class="jtree__key">${key}</div>
                    <div class="jtree__value jtree__value--${type}">${value}</div>
                </div>`;
    }

    function createCollapsibleItem (key, value, type, children)
    {
        var checked = collapsible ? 'checked' : '';
        
        return `<label class="jtree__item jtree__item--collapsible">
                     <input type="checkbox" ${checked} class="jtree__toggle"/>
                     <div class="jtree__ckey">${key}</div>
                     ${children}
                </label>`;
    }

    function handleChildren(key, value, type) {
        var html = '';

        for(var item in value) { 
            var _key = item,
                _val = value[item];

            html += handleItem(_key, _val);
        }

        return createCollapsibleItem(key, value, type, html);
    }

    function handleItem(key, value) {
        var type = typeof value;

        if(type === 'object') {        
            return handleChildren(key, value, type);
        }

        return createItem(key, value, type);
    }

    function parseObject(obj, pad=5) {
        html = '<div class="jtree">';

        for(var item in obj) { 
            var key = item,
                value = obj[item];

            html += handleItem(key, value);
        }

        for (let i = 0; i < pad; ++i)
            html += createItem("", "", "string")
        

        html += '</div>';

        return html;
    }

    return parseObject(json);
};

/** 
 * Creates a tree view based on the specified tree. The root of the tree is
 * inserted into the specified parent elment.
 *
 *
 * Each level in the tree must be an Object with at least a 'value'
 * property. For leaves, the value can be a string/number etc, for branches
 * the value is another Object or Array containing the children.
 *
 * If present, the content of a 'doc' property is used to create the node's
 * documentation (probably in a tooltip).
 */
function createTreeView (json, expanded = false, pad = 5) {

    function createTooltip (text) {
        return $(`<span class='jtree__tooltip' onClick='$(this).hide()'><pre>${text}</pre></span>`)
    }

    function createLeaf (key, value, type) {
        const leaf = $(`<div class='jtree__item'>`)
        
        leaf.append($(`<div class='jtree__key'>${key}</div>`))
        leaf.append($(`<div class='jtree__value jtree__value--${type}'>${value}</div>`))

        return leaf
    }

    function createBranch (key, children, type) {
        const branch = $(`<label class='jtree__item jtree__item--collapsible'>`)

        branch.append($(`<input type='checkbox' class='jtree__toggle'>`).prop('checked', expanded))
        branch.append($(`<div class='jtree__ckey'>${key}</div>`))

        return branch.append(createChildren(children))
    }
    
    function createElement (key, node) {
        const type = typeof node.value;
       //console.log(key)

        if (type === 'object') {
            var element = createBranch(key, node.value, type)
        } else {
            var element = createLeaf(key, node.value, type)
        }

        if (node.doc !== undefined)
            element.children('.jtree__ckey,.jtree__key').append(createTooltip(node.doc))
        
        return element;
    }

    // Returns an array with one element for every (k, v) in the children
    // iterable
    function createChildren (children) {
        return $.map(children, (value, key) => createElement(key, value))
    }

    // Returns an array of `pad` dummy leaves
    function createPad (pad) {
        return $.map(Array(pad), () => createLeaf("", "", "string"))
    }

    return $("<div class='jtree'>").append(createChildren(json)).append(createPad(pad))
}


function tree_json () {
    // Note the top level has the same layout as 'value' entries for non-terminal nodes.
    return {
        'Users' : { 'doc' : 'Lists all the users in the system',
                    'value' : {
                        'random' : {
                            'doc' : 'User entry',
                            'value' : {
                                'name'    : {
                                    'value' : 'J Random',
                                    'doc' : '',
                                },
                                'surname' : {
                                    'value' : 'Hacker',
                                    'doc' : '',
                                    'housekeeping' : '',
                                },
                                'email'   : { 'value' : 'jrhacker@bigcompany.com', },
                            },
                            'housekeeping' : { // ignored
                                'housekeeping things' : 10,
                                'schema' : null,
                            },
                            'more houskeping'  : null // again, ignored
                        },
                        'john' : {
                            'doc' : 'User entry',
                            'value' : {
                                'name'    : {'value' : 'John'},
                                'surname' : {'value' : 'Doe'},
                                'email'   : {'value' : 'john.doe@acme.com'},
                            },
                            'schema' : null, // ignored
                        },
                        'jane' : {
                            'doc' : 'User entry',
                            'value' : {
                                'name'    : {'value' : 'Jane'},
                                'surname' : {'value' : 'Doe'},
                                'email'   : {'value' : 'jane.doe@acme.com'},
                            },
                            'modified' : true, // ignored
                        },
                    }
                  },
        'Log' : { 'doc' : 'Log lines',
                  'value' : [
                      {'value': 'Message 1'},
                      {'value': 'Message 2'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'},
                      {'value': 'Message 3'}
                  ]
                }
    };
}

function some_json () {
    return {
        'User' : {
            'Personal Info': {
                'Name': 'Eddy',
                'Age': 3
            },
            'Active': true,
            'Messages': [
                'Message 1',
                'Message 2',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3',
                'Message 3'
            ]
        },
        'Total': 1
    }
};

