#!/usr/bin/env node

var fs = require('fs');
var path = require('path');
var child_process = require('child_process');

var usage = 'usage: render.js <stylesheet> <image>';

var stylesheet = process.ARGV[2];
if (!stylesheet) {
   console.log(usage);
   process.exit(1);
}

var image = process.ARGV[3];
if (!image) {
   console.log(usage);
   process.exit(1);
}

var mapnik = require('mapnik');

function renderMap(stylesheet, image) {
    var map = new mapnik.Map(600, 400);
    map.load(stylesheet);
    map.zoom_all();
    map.render_to_file(image);
    child_process.exec('open ' + image);
}

if (path.extname(stylesheet).match(/.mml/i)) {
    try {
        var carto = require('carto');
        new carto.Renderer({
            filename: stylesheet,
            local_data_dir: path.dirname(stylesheet),
        }).render(fs.readFileSync(stylesheet, 'utf-8'), function(err, output) {
            if (err) {
                if (Array.isArray(err)) {
                    err.forEach(function(e) {
                        carto.writeError(e, options);
                    });
                }
                process.exit(1);
            } else {
                fs.writeFileSync(
                    stylesheet.replace('mml', 'xml'),
                    output, 'utf-8');
                stylesheet = stylesheet.replace('mml', 'xml');
                renderMap(stylesheet, image);
            }
        });
    } catch(e) {
        console.log('Carto is required to render .mml files.')
        process.exit(1);
    }
} else {
    renderMap(stylesheet, image);
}
