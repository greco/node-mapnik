#!/usr/bin/env node

/*
Example of streaming features into Mapnik using a
javascript callback that leverages experimental javascript
datasource support
*/

/*
NOTE - maps using mapnik.JSDatasource can only be rendered with
mapnik.render_to_string() or mapnik.render_to_file() as the javascript
callback only works if the rendering happens in the main thread.

If you want async rendering using mapnik.render() then use the
mapnik.MemoryDatasource instead of mapnik.JSDatasource.
*/

var mapnik = require('mapnik');
var sys = require('fs');
var path = require('path');
var merc = '+proj=merc +a=6378137 +b=6378137 +lat_ts=0.0 +lon_0=0.0 +x_0=0.0 +y_0=0 +k=1.0 +units=m +nadgrids=@null +no_defs +over';

// map with just a style
// eventually the api will support adding styles in javascript
var s = '<Map srs="' + merc + '">';
s += '<Style name="points">';
s += ' <Rule>';
s += '  <Filter>[POP2005]&gt;200000000</Filter>';
s += '  <MarkersSymbolizer marker-type="ellipse" fill="red" width="10" allow-overlap="true" placement="point"/>';
s += ' </Rule>';
s += ' <Rule>';
s += '  <Filter>[POP2005]&gt;20000000</Filter>';
s += '  <MarkersSymbolizer marker-type="ellipse" fill="green" width="7" allow-overlap="true" placement="point"/>';
s += ' </Rule>';
s += ' <Rule>';
s += '  <ElseFilter />';
s += '  <MarkersSymbolizer marker-type="ellipse" fill="blue" width="5" allow-overlap="true" placement="point"/>';
s += ' </Rule>';
s += '</Style>';
s += '</Map>';

// create map object
var map = new mapnik.Map(256,256);
map.fromStringSync(s,{strict:true,base:'.'});

// go get some arbitrary data that we can stream
var shp = path.join(__dirname,'../data/world_merc');

var ds = new mapnik.Datasource({
    type: 'shape',
    file: shp
});

// get the featureset that exposes lazy next() iterator
var featureset = ds.featureset();

// now construct a callback based javascript datasource
var options = {
    // right now 'extent' is required
    // world in merc
    extent: '-20037508.342789,-8283343.693883,20037508.342789,18365151.363070',
    // world in long lat
    //extent: '-180,-90,180,90',
};


// Define a callback to pass to the javascript datasource
// this will be called repeatedly as mapnik renders
// Rendering will continue until the function
// no longer returns a valid object of x,y,properties

// WARNING - this API will change!

var feat;
var next = function() {
    while (feat = featureset.next(true)) {
        // center longitude of polygon bbox
        var x = (feat._extent[0]+feat._extent[2])/2;
        // center latitude of polygon bbox
        var y = (feat._extent[1]+feat._extent[3])/2;
        return { 'x'          : x,
                 'y'          : y,
                 'properties' : { 'NAME':feat.NAME,'POP2005':feat.POP2005 }
               };
    }
}

// create the special datasource
var ds = new mapnik.JSDatasource(options,next);

// contruct a mapnik layer dynamically
var l = new mapnik.Layer('test');
l.srs = map.srs;
l.styles = ["points"];

// add our custom datasource
l.datasource = ds;

// add this layer to the map
map.add_layer(l);

// zoom to the extent of the new layer (pulled from options since otherwise we cannot know)
map.zoom_all();

// render it! You should see a bunch of red and blue points reprenting
map.render_to_file('js_points.png');

console.log('rendered to js_points.png!' );