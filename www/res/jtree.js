/**
 * @file jtree.js
 *
 * @brief Tree viewer widget for RestConf.
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

function jtreeViewer(json, collapsible=false) {
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

        if(typeof value === 'object') {        
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

