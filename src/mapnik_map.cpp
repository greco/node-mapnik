
#include <node_buffer.h>
#include <node_version.h>

// mapnik
#include <mapnik/version.hpp>
#include <mapnik/map.hpp>
#include <mapnik/projection.hpp>
#include <mapnik/layer.hpp>
#include <mapnik/filter_factory.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/config_error.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/save_map.hpp>
#include <mapnik/query.hpp>
#include <mapnik/ctrans.hpp>

// renderers
#include <mapnik/agg_renderer.hpp>

// provides MAPNIK_SUPPORTS_GRID_RENDERER
#include <mapnik/config.hpp>
#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)
#include <mapnik/grid/grid_renderer.hpp>
#else
#include "grid/grid.h"
#include "grid/renderer.h"
#include "grid/grid_buffer.h"
#include "agg/agg_conv_stroke.h"
// ellipse drawing
#include <math.h>
#endif

#if defined(HAVE_CAIRO)
#include <mapnik/cairo_renderer.hpp>
#endif

// stl
#include <exception>
#include <set>

// boost
#include <boost/foreach.hpp>

#include "utils.hpp"
#include "mapnik_map.hpp"
#include "ds_emitter.hpp"
#include "layer_emitter.hpp"
#include "mapnik_layer.hpp"

Persistent<FunctionTemplate> Map::constructor;

void Map::Initialize(Handle<Object> target) {

    HandleScope scope;

    constructor = Persistent<FunctionTemplate>::New(FunctionTemplate::New(Map::New));
    constructor->InstanceTemplate()->SetInternalFieldCount(1);
    constructor->SetClassName(String::NewSymbol("Map"));

    NODE_SET_PROTOTYPE_METHOD(constructor, "load", load);
    NODE_SET_PROTOTYPE_METHOD(constructor, "loadSync", loadSync);
    NODE_SET_PROTOTYPE_METHOD(constructor, "fromStringSync", fromStringSync);
    NODE_SET_PROTOTYPE_METHOD(constructor, "fromString", fromString);
    NODE_SET_PROTOTYPE_METHOD(constructor, "save", save);
    NODE_SET_PROTOTYPE_METHOD(constructor, "clear", clear);
    NODE_SET_PROTOTYPE_METHOD(constructor, "toXML", to_string);
    NODE_SET_PROTOTYPE_METHOD(constructor, "resize", resize);

#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)
    NODE_SET_PROTOTYPE_METHOD(constructor, "render_grid", render_grid);
#else
    // private method as this will soon be removed
    NODE_SET_PROTOTYPE_METHOD(constructor, "_render_grid", render_grid);
#endif

    NODE_SET_PROTOTYPE_METHOD(constructor, "zoom_all", zoom_all);
    NODE_SET_PROTOTYPE_METHOD(constructor, "zoom_to_box", zoom_to_box);
    NODE_SET_PROTOTYPE_METHOD(constructor, "render", render);
    NODE_SET_PROTOTYPE_METHOD(constructor, "renderSync", renderSync);
    NODE_SET_PROTOTYPE_METHOD(constructor, "renderFileSync", renderFileSync);
    NODE_SET_PROTOTYPE_METHOD(constructor, "scaleDenominator", scaleDenominator);

    // layer access
    NODE_SET_PROTOTYPE_METHOD(constructor, "add_layer", add_layer);
    NODE_SET_PROTOTYPE_METHOD(constructor, "get_layer", get_layer);

    // temp hack to expose layer metadata
    NODE_SET_PROTOTYPE_METHOD(constructor, "layers", layers);
    NODE_SET_PROTOTYPE_METHOD(constructor, "features", features);
    NODE_SET_PROTOTYPE_METHOD(constructor, "describe_data", describe_data);

    // properties
    ATTR(constructor, "srs", get_prop, set_prop);
    ATTR(constructor, "width", get_prop, set_prop);
    ATTR(constructor, "height", get_prop, set_prop);
    ATTR(constructor, "bufferSize", get_prop, set_prop);
    ATTR(constructor, "extent", get_prop, set_prop);
    ATTR(constructor, "maximumExtent", get_prop, set_prop);

    target->Set(String::NewSymbol("Map"),constructor->GetFunction());
    //eio_set_max_poll_reqs(10);
    //eio_set_min_parallel(10);
}

Map::Map(int width, int height) :
  ObjectWrap(),
  map_(new mapnik::Map(width,height)) {}

Map::Map(int width, int height, std::string const& srs) :
  ObjectWrap(),
  map_(new mapnik::Map(width,height,srs)) {}

Map::~Map()
{
    // std::clog << "~Map(node)\n";
    // release is handled by boost::shared_ptr
}

Handle<Value> Map::New(const Arguments& args)
{
    HandleScope scope;

    if (!args.IsConstructCall())
        return ThrowException(String::New("Cannot call constructor as function, you need to use 'new' keyword"));

    // accept a reference or v8:External?
    if (args[0]->IsExternal())
    {
        return ThrowException(String::New("No support yet for passing v8:External wrapper around C++ void*"));
    }

    if (args.Length() == 2)
    {
        if (!args[0]->IsNumber() || !args[1]->IsNumber())
            return ThrowException(Exception::Error(
               String::New("'width' and 'height' must be a integers")));
        Map* m = new Map(args[0]->IntegerValue(),args[1]->IntegerValue());
        m->Wrap(args.This());
        return args.This();
    }
    else if (args.Length() == 3)
    {
        if (!args[0]->IsNumber() || !args[1]->IsNumber() || !args[2]->IsString())
            return ThrowException(Exception::Error(
               String::New("'width' and 'height' must be a integers")));
        Map* m = new Map(args[0]->IntegerValue(),args[1]->IntegerValue(),TOSTR(args[2]));
        m->Wrap(args.This());
        return args.This();
    }
    else
    {
        return ThrowException(Exception::Error(
          String::New("please provide Map width and height and optional srs")));
    }
    //return args.This();
    return Undefined();
}

Handle<Value> Map::get_prop(Local<String> property,
                         const AccessorInfo& info)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(info.This());
    std::string a = TOSTR(property);
    if(a == "extent") {
        Local<Array> a = Array::New(4);
        mapnik::box2d<double> const& e = m->map_->get_current_extent();
        a->Set(0, Number::New(e.minx()));
        a->Set(1, Number::New(e.miny()));
        a->Set(2, Number::New(e.maxx()));
        a->Set(3, Number::New(e.maxy()));
        return scope.Close(a);
    }
    else if(a == "maximumExtent") {
        Local<Array> a = Array::New(4);
        boost::optional<mapnik::box2d<double> > const& e = m->map_->maximum_extent();
        if (!e)
            return Undefined();
        a->Set(0, Number::New(e->minx()));
        a->Set(1, Number::New(e->miny()));
        a->Set(2, Number::New(e->maxx()));
        a->Set(3, Number::New(e->maxy()));
        return scope.Close(a);
    }
    else if (a == "srs")
        return scope.Close(String::New(m->map_->srs().c_str()));
    else if(a == "bufferSize")
        return scope.Close(Integer::New(m->map_->buffer_size()));
    else if(a == "width")
        return scope.Close(Integer::New(m->map_->width()));
    else if(a == "height")
        return scope.Close(Integer::New(m->map_->height()));
    // TODO - expose mapnik.Color
    //else if(a == "backgroundColor")
    return Undefined();
}

void Map::set_prop(Local<String> property,
                         Local<Value> value,
                         const AccessorInfo& info)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(info.Holder());
    std::string a = TOSTR(property);
    if(a == "extent" || a == "maximumExtent") {
        if (!value->IsArray()) {
            ThrowException(Exception::Error(
               String::New("Must provide an array of: [minx,miny,maxx,maxy]")));
        } else {
            Local<Array> arr = Local<Array>::Cast(value);
            if (!arr->Length() == 4) {
                ThrowException(Exception::Error(
                   String::New("Must provide an array of: [minx,miny,maxx,maxy]")));
            } else {
                double minx = arr->Get(0)->NumberValue();
                double miny = arr->Get(1)->NumberValue();
                double maxx = arr->Get(2)->NumberValue();
                double maxy = arr->Get(3)->NumberValue();
                mapnik::box2d<double> box(minx,miny,maxx,maxy);
                if(a == "extent")
                    m->map_->zoom_to_box(box);
                else
                    m->map_->set_maximum_extent(box);
            }
        }
    }
    else if (a == "srs")
    {
        if (!value->IsString()) {
            ThrowException(Exception::Error(
               String::New("'srs' must be a string")));
        } else {
            m->map_->set_srs(TOSTR(value));
        }
    }
    else if(a == "bufferSize") {
        if (!value->IsNumber()) {
            ThrowException(Exception::Error(
               String::New("Must provide an integer bufferSize")));
        } else {
            m->map_->set_buffer_size(value->IntegerValue());
        }
    }
    else if(a == "width") {
        if (!value->IsNumber()) {
            ThrowException(Exception::Error(
               String::New("Must provide an integer width")));
        } else {
            m->map_->set_width(value->IntegerValue());
        }
    }
    else if(a == "height") {
        if (!value->IsNumber()) {
            ThrowException(Exception::Error(
               String::New("Must provide an integer height")));
        } else {
            m->map_->set_height(value->IntegerValue());
        }
    }
}

Handle<Value> Map::scaleDenominator(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    return scope.Close(Number::New(m->map_->scale_denominator()));
}

Handle<Value> Map::add_layer(const Arguments &args) {
    HandleScope scope;
    Local<Object> obj = args[0]->ToObject();
    if (args[0]->IsNull() || args[0]->IsUndefined() || !Layer::constructor->HasInstance(obj))
      return ThrowException(Exception::TypeError(String::New("mapnik.Layer expected")));
    Layer *l = ObjectWrap::Unwrap<Layer>(obj);
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    // TODO - addLayer should be add_layer in mapnik
    m->map_->addLayer(*l->get());
    return Undefined();
}

Handle<Value> Map::get_layer(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer name or index")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::vector<mapnik::layer> & layers = m->map_->layers();

    Local<Value> layer = args[0];
    if (layer->IsNumber())
    {
        unsigned int index = args[0]->IntegerValue();

        if (index < layers.size())
        {
            return scope.Close(Layer::New(layers[index]));
        }
        else
        {
          return ThrowException(Exception::TypeError(
            String::New("invalid layer index")));
        }    
    }
    else if (layer->IsString())
    {
        bool found = false;
        unsigned int idx(0);
        std::string const & layer_name = TOSTR(layer);
        BOOST_FOREACH ( mapnik::layer const& lyr, layers )
        {
            if (lyr.name() == layer_name)
            {
                found = true;
                return scope.Close(Layer::New(layers[idx]));
            }
            ++idx; 
        }
        if (!found) 
        {
            std::ostringstream s;
            s << "Layer name '" << layer_name << "' not found";
            return ThrowException(Exception::TypeError(
              String::New(s.str().c_str())));
        }
    
    }
    else
    {
        return ThrowException(Exception::TypeError(
           String::New("first argument must be either a layer name(string) or layer index (integer)")));
    }

    return Undefined();

}


Handle<Value> Map::layers(const Arguments& args)
{
    HandleScope scope;

    // todo - optimize by allowing indexing...
    /*if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));
    */

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();
    Local<Array> a = Array::New(layers.size());

    for (unsigned i = 0; i < layers.size(); ++i )
    {
        const mapnik::layer & layer = layers[i];
        Local<Object> meta = Object::New();
        layer_as_json(meta,layer);
        a->Set(i, meta);
    }

    return scope.Close(a);

}

Handle<Value> Map::describe_data(const Arguments& args)
{
    HandleScope scope;

    // todo - optimize by allowing indexing...
    /*if (!args.Length() == 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));
    */

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();

    Local<Object> meta = Object::New();

    for (unsigned i = 0; i < layers.size(); ++i )
    {
        const mapnik::layer & layer = layers[i];
        Local<Object> description = Object::New();
        mapnik::datasource_ptr ds = layer.datasource();
        if (ds)
        {
            describe_datasource(description,ds);
        }
        meta->Set(String::NewSymbol(layer.name().c_str()), description);
    }

    return scope.Close(meta);

}


Handle<Value> Map::features(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 1)
      return ThrowException(Exception::Error(
        String::New("Please provide layer index")));

    if (!args[0]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("layer index must be an integer")));

    unsigned first = 0;
    unsigned last = 0;

    // we are slicing
    if (args.Length() == 3)
    {
        if (!args[1]->IsNumber() || !args[2]->IsNumber())
            return ThrowException(Exception::Error(
               String::New("Index of 'first' and 'last' feature must be an integer")));
        first = args[1]->IntegerValue();
        last = args[2]->IntegerValue();
    }

    unsigned index = args[0]->IntegerValue();

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::vector<mapnik::layer> const & layers = m->map_->layers();

    // TODO - we don't know features.length at this point
    Local<Array> a = Array::New(0);
    if ( index < layers.size())
    {
        mapnik::layer const& layer = layers[index];
        mapnik::datasource_ptr ds = layer.datasource();
        if (ds)
        {
            datasource_features(a,ds,first,last);
        }
    }

    return scope.Close(a);

}

Handle<Value> Map::clear(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    m->map_->remove_all();
    return Undefined();
}

Handle<Value> Map::resize(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() == 2)
      return ThrowException(Exception::Error(
        String::New("Please provide width and height")));

    if (!args[0]->IsNumber() || !args[1]->IsNumber())
      return ThrowException(Exception::TypeError(
        String::New("width and height must be integers")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    m->map_->resize(args[0]->IntegerValue(),args[1]->IntegerValue());
    return Undefined();
}



typedef struct {
    Map *m;
    std::string stylesheet;
    bool strict;
    bool error;
    std::string error_name;
    Persistent<Function> cb;
} load_closure_t;


Handle<Value> Map::load(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 2)
      return ThrowException(Exception::Error(
        String::New("please provide a stylesheet path, options, and callback")));

    // ensure stylesheet path is a string
    Local<Value> stylesheet = args[0];
    if (!stylesheet->IsString())
        return ThrowException(Exception::TypeError(
           String::New("first argument must be a path to a mapnik stylesheet")));

    // ensure callback is a function
    Local<Value> callback = args[args.Length()-1];
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    // ensure options object
    if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("options must be an object, eg {strict: true}")));

    Local<Object> options = args[1]->ToObject();

    bool strict = false;
    Local<String> param = String::New("strict");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsBoolean())
          return ThrowException(Exception::TypeError(
            String::New("'strict' must be a Boolean")));
        strict = param_val->BooleanValue();
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    load_closure_t *closure = new load_closure_t();
    
    closure->stylesheet = TOSTR(stylesheet);
    closure->m = m;
    closure->strict = strict;
    closure->error = false;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(callback));
    eio_custom(EIO_Load, EIO_PRI_DEFAULT, EIO_AfterLoad, closure);
    ev_ref(EV_DEFAULT_UC);
    m->Ref();
    
    return Undefined();
}

int Map::EIO_Load(eio_req *req)
{
    load_closure_t *closure = static_cast<load_closure_t *>(req->data);

    try
    {
        mapnik::load_map(*closure->m->map_,closure->stylesheet,closure->strict);
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "unknown exception happened while rendering the map,\n this should not happen, please submit a bug report";
    }
    return 0;
}

int Map::EIO_AfterLoad(eio_req *req)
{
    HandleScope scope;

    load_closure_t *closure = static_cast<load_closure_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(closure->m->handle_) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    return 0;
}


Handle<Value> Map::loadSync(const Arguments& args)
{
    HandleScope scope;
    if (args.Length() != 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to a mapnik stylesheet")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& stylesheet = TOSTR(args[0]);
    bool strict = false;
    try
    {
        mapnik::load_map(*m->map_,stylesheet,strict);
    }
    catch (const mapnik::config_error & ex )
    {
      return ThrowException(Exception::Error(
        String::New(ex.what())));
    }
    catch (...)
    {
      return ThrowException(Exception::TypeError(
        String::New("something went wrong loading the map")));
    }
    return Undefined();
}

Handle<Value> Map::fromStringSync(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() >= 1) {
        return ThrowException(Exception::TypeError(
        String::New("Accepts 2 arguments: stylesheet string and options")));
    }

    if (!args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a mapnik stylesheet string")));

    // ensure options object
    if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("options must be an object, eg {strict: true, base: \".\"'}")));

    Local<Object> options = args[1]->ToObject();

    bool strict = false;
    Local<String> param = String::New("strict");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsBoolean())
          return ThrowException(Exception::TypeError(
            String::New("'strict' must be a Boolean")));
        strict = param_val->BooleanValue();
    }

    std::string base_url("");
    param = String::New("base");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsString())
          return ThrowException(Exception::TypeError(
            String::New("'base' must be a string representing a filesystem path")));
        base_url = TOSTR(param_val);
    }


    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    std::string const& stylesheet = TOSTR(args[0]);

    try
    {
        mapnik::load_map_string(*m->map_,stylesheet,strict,base_url);
    }
    catch (const mapnik::config_error & ex )
    {
      return ThrowException(Exception::Error(
        String::New(ex.what())));
    }
    catch (...)
    {
      return ThrowException(Exception::TypeError(
        String::New("something went wrong loading the map")));
    }
    return Undefined();
}

typedef struct {
    Map *m;
    std::string stylesheet;
    std::string base_url;
    bool strict;
    bool error;
    std::string error_name;
    Persistent<Function> cb;
} load_string_closure_t;


Handle<Value> Map::fromString(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 2)
      return ThrowException(Exception::Error(
        String::New("please provide a stylesheet string, options, and callback")));

    // ensure stylesheet path is a string
    Local<Value> stylesheet = args[0];
    if (!stylesheet->IsString())
        return ThrowException(Exception::TypeError(
           String::New("first argument must be a path to a mapnik stylesheet string")));

    // ensure callback is a function
    Local<Value> callback = args[args.Length()-1];
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    // ensure options object
    if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("options must be an object, eg {strict: true, base: \".\"'}")));

    Local<Object> options = args[1]->ToObject();

    bool strict = false;
    Local<String> param = String::New("strict");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsBoolean())
          return ThrowException(Exception::TypeError(
            String::New("'strict' must be a Boolean")));
        strict = param_val->BooleanValue();
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    load_string_closure_t *closure = new load_string_closure_t();

    param = String::New("base");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsString())
          return ThrowException(Exception::TypeError(
            String::New("'base' must be a string representing a filesystem path")));
        closure->base_url = TOSTR(param_val);
    }
    
    closure->stylesheet = TOSTR(stylesheet);
    closure->m = m;
    closure->strict = strict;
    closure->error = false;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(callback));
    eio_custom(EIO_FromString, EIO_PRI_DEFAULT, EIO_AfterFromString, closure);
    ev_ref(EV_DEFAULT_UC);
    m->Ref();
    
    return Undefined();
}

int Map::EIO_FromString(eio_req *req)
{
    load_string_closure_t *closure = static_cast<load_string_closure_t *>(req->data);

    try
    {
        mapnik::load_map_string(*closure->m->map_,closure->stylesheet,closure->strict,closure->base_url);
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "unknown exception happened while rendering the map,\n this should not happen, please submit a bug report";
    }
    return 0;
}

int Map::EIO_AfterFromString(eio_req *req)
{
    HandleScope scope;

    load_string_closure_t *closure = static_cast<load_string_closure_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(closure->m->handle_) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    return 0;
}


Handle<Value> Map::save(const Arguments& args)
{
    HandleScope scope;
    if (args.Length() != 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to map.xml to save")));

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& filename = TOSTR(args[0]);
    bool explicit_defaults = false;
    mapnik::save_map(*m->map_,filename,explicit_defaults);
    return Undefined();
}

Handle<Value> Map::to_string(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    bool explicit_defaults = false;
    std::string map_string = mapnik::save_map_to_string(*m->map_,explicit_defaults);
    return scope.Close(String::New(map_string.c_str()));
}

Handle<Value> Map::zoom_all(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    try {
      m->map_->zoom_all();
    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("Unknown exception happened while zooming, please submit a bug report")));
    }
    return Undefined();
}

Handle<Value> Map::zoom_to_box(const Arguments& args)
{
    HandleScope scope;
    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    double minx;
    double miny;
    double maxx;
    double maxy;

    if (args.Length() == 1)
    {
        if (!args[0]->IsArray())
            return ThrowException(Exception::Error(
               String::New("Must provide an array of: [minx,miny,maxx,maxy]")));
        Local<Array> a = Local<Array>::Cast(args[0]);
        minx = a->Get(0)->NumberValue();
        miny = a->Get(1)->NumberValue();
        maxx = a->Get(2)->NumberValue();
        maxy = a->Get(3)->NumberValue();

    }
    else if (args.Length() != 4)
      return ThrowException(Exception::Error(
        String::New("Must provide 4 arguments: minx,miny,maxx,maxy")));
    else {
        minx = args[0]->NumberValue();
        miny = args[1]->NumberValue();
        maxx = args[2]->NumberValue();
        maxy = args[3]->NumberValue();
    }
    mapnik::box2d<double> box(minx,miny,maxx,maxy);
    m->map_->zoom_to_box(box);
    return Undefined();
}

typedef struct {
    Map *m;
    std::string format;
    mapnik::box2d<double> bbox;
    bool error;
    std::string error_name;
    std::string im_string;
    Persistent<Function> cb;
} closure_t;

Handle<Value> Map::render(const Arguments& args)
{
    HandleScope scope;

    /*
    std::clog << "eio_nreqs" << eio_nreqs() << "\n";
    std::clog << "eio_nready" << eio_nready() << "\n";
    std::clog << "eio_npending" << eio_npending() << "\n";
    std::clog << "eio_nthreads" << eio_nthreads() << "\n";
    */

    if (args.Length() < 3)
        return ThrowException(Exception::TypeError(
          String::New("requires three arguments, a extent array, a format, and a callback")));

    // extent array
    if (!args[0]->IsArray())
        return ThrowException(Exception::TypeError(
           String::New("first argument must be an extent array of: [minx,miny,maxx,maxy]")));

    // format
    if (!args[1]->IsString())
        return ThrowException(Exception::TypeError(
           String::New("second argument must be an format string")));

    // function callback
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    Local<Array> a = Local<Array>::Cast(args[0]);
    uint32_t a_length = a->Length();
    if (!a_length  == 4) {
        return ThrowException(Exception::TypeError(
           String::New("first argument must be 4 item array of: [minx,miny,maxx,maxy]")));
    }

    closure_t *closure = new closure_t();

    if (!closure) {
      V8::LowMemoryNotification();
      return ThrowException(Exception::Error(
            String::New("Could not allocate enough memory")));
    }

    double minx = a->Get(0)->NumberValue();
    double miny = a->Get(1)->NumberValue();
    double maxx = a->Get(2)->NumberValue();
    double maxy = a->Get(3)->NumberValue();

    Map* m = ObjectWrap::Unwrap<Map>(args.This());

    closure->m = m;
    closure->format = TOSTR(args[1]);
    closure->error = false;
    closure->bbox = mapnik::box2d<double>(minx,miny,maxx,maxy);
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(args[args.Length()-1]));
    eio_custom(EIO_Render, EIO_PRI_DEFAULT, EIO_AfterRender, closure);
    ev_ref(EV_DEFAULT_UC);
    m->Ref();
    return Undefined();
}

int Map::EIO_Render(eio_req *req)
{
    closure_t *closure = static_cast<closure_t *>(req->data);

    // zoom to
    closure->m->map_->zoom_to_box(closure->bbox);
    try
    {
        mapnik::image_32 im(closure->m->map_->width(),closure->m->map_->height());
        mapnik::agg_renderer<mapnik::image_32> ren(*closure->m->map_,im);
        ren.apply();
        closure->im_string = save_to_string(im, closure->format);
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "unknown exception happened while rendering the map,\n this should not happen, please submit a bug report";
    }
    return 0;
}

int Map::EIO_AfterRender(eio_req *req)
{
    HandleScope scope;

    closure_t *closure = static_cast<closure_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        // TODO - add more attributes
        // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Error
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        #if NODE_VERSION_AT_LEAST(0,3,0)
          node::Buffer *retbuf = Buffer::New((char *)closure->im_string.data(),closure->im_string.size());
        #else
          node::Buffer *retbuf = Buffer::New(closure->im_string.size());
          memcpy(retbuf->data(), closure->im_string.data(), closure->im_string.size());
        #endif
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(retbuf->handle_) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    return 0;
}

Handle<Value> Map::renderSync(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("argument must be a format string")));

    std::string format = TOSTR(args[0]);

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string s;
    try
    {
        mapnik::image_32 im(m->map_->width(),m->map_->height());
        mapnik::agg_renderer<mapnik::image_32> ren(*m->map_,im);
        ren.apply();
        //std::string ss = mapnik::save_to_string<mapnik::image_data_32>(im.data(),"png");
        s = save_to_string(im, format);

    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("unknown exception happened while rendering the map, please submit a bug report")));
    }

    #if NODE_VERSION_AT_LEAST(0,3,0)
      node::Buffer *retbuf = Buffer::New((char*)s.data(),s.size());
    #else
      node::Buffer *retbuf = Buffer::New(s.size());
      memcpy(retbuf->data(), s.data(), s.size());
    #endif

    return scope.Close(retbuf->handle_);
}

Handle<Value> Map::renderFileSync(const Arguments& args)
{
    HandleScope scope;
    if (!args.Length() >= 1 || !args[0]->IsString())
      return ThrowException(Exception::TypeError(
        String::New("first argument must be a path to a file to save")));

    if (args.Length() > 2)
      return ThrowException(Exception::TypeError(
        String::New("accepts two arguments, a required path to a file, and an optional options object, eg. {format: 'pdf'}")));

    std::string format("");

    if (args.Length() == 2){
      if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("second argument is optional, but if provided must be an object, eg. {format: 'pdf'}")));

        Local<Object> options = args[1]->ToObject();
        if (options->Has(String::New("format")))
        {
            Local<Value> format_opt = options->Get(String::New("format"));
            if (!format_opt->IsString())
              return ThrowException(Exception::TypeError(
                String::New("'format' must be a String")));

            format = TOSTR(format_opt);
        }
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    std::string const& output = TOSTR(args[0]);

    if (format.empty()) {
        format = mapnik::guess_type(output);
        if (format == "<unknown>") {
            std::ostringstream s("");
            s << "unknown output extension for: " << output << "\n";
            return ThrowException(Exception::Error(
                String::New(s.str().c_str())));
        }
    }

    try
    {

        if (format == "pdf" || format == "svg" || format =="ps" || format == "ARGB32" || format == "RGB24")
        {
    #if defined(HAVE_CAIRO)
            mapnik::save_to_cairo_file(*m->map_,output,format);
    #else
            std::ostringstream s("");
            s << "Cairo backend is not available, cannot write to " << format << "\n";
            return ThrowException(Exception::Error(
              String::New(s.str().c_str())));
    #endif
        }
        else
        {
            mapnik::image_32 im(m->map_->width(),m->map_->height());
            mapnik::agg_renderer<mapnik::image_32> ren(*m->map_,im);
            ren.apply();
            mapnik::save_to_file<mapnik::image_data_32>(im.data(),output);
        }
    }
    catch (const mapnik::config_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::datasource_exception & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::proj_init_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const std::runtime_error & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (std::exception & ex)
    {
        return ThrowException(Exception::Error(
          String::New(ex.what())));
    }
    catch (...)
    {
        return ThrowException(Exception::TypeError(
          String::New("unknown exception happened while rendering the map, please submit a bug report")));
    }
    return Undefined();
}

#if defined(MAPNIK_SUPPORTS_GRID_RENDERER)

struct grid_t {
    Map *m;
    boost::shared_ptr<mapnik::grid> grid_ptr;
    std::size_t layer_idx;
    std::string layer_name;
    std::string join_field;
    uint32_t num_fields;
    int size;
    bool error;
    std::string error_name;
    bool include_features;
    Persistent<Function> cb;
    bool grid_initialized;
};

Handle<Value> Map::render_grid(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 2)
      return ThrowException(Exception::Error(
        String::New("please provide layer name or index, options, and callback")));

    // make sure layer name is a string
    Local<Value> layer = args[0];
    if (! (layer->IsString() || layer->IsNumber()) )
        return ThrowException(Exception::TypeError(
           String::New("first argument must be either a layer name(string) or layer index (integer)")));

    // ensure callback is a function
    Local<Value> callback = args[args.Length()-1];
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    // ensure options object
    if (!args[1]->IsObject())
        return ThrowException(Exception::TypeError(
          String::New("options must be an object, eg {key: '__id__', resolution : 4, fields: ['name']}")));
    
    Local<Object> options = args[1]->ToObject();

    std::string join_field("__id__");
    Local<String> param = String::New("key");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsString())
          return ThrowException(Exception::TypeError(
            String::New("'key' must be a string")));
        join_field = TOSTR(param_val);
    }

    unsigned int step(4);
    param = String::New("resolution");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsNumber())
          return ThrowException(Exception::TypeError(
            String::New("'resolution' must be an integer")));
        step = param_val->IntegerValue();
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    
    /*    
    // http://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
    if (!(w && !(w & (w - 1)))) {
        return ThrowException(Exception::Error(
            String::New("Map width and height must be a power of two")));    
    }*/

    grid_t *closure = new grid_t();

    if (!closure) {
        V8::LowMemoryNotification();
        return ThrowException(Exception::Error(
            String::New("Could not allocate enough memory")));
    }

    if (layer->IsString()) {
        closure->layer_name = TOSTR(layer);
    } else if (layer->IsNumber()) {
        closure->layer_idx = static_cast<std::size_t>(layer->NumberValue());
    }

    closure->m = m;
    closure->join_field = join_field;
    closure->error = false;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(callback));
    closure->num_fields = 0;
    
    unsigned int grid_width = m->map_->width()/step;
    unsigned int grid_height = m->map_->height()/step;

    closure->grid_ptr = boost::shared_ptr<mapnik::grid>(
                new mapnik::grid(grid_width,grid_height,closure->join_field,step)
            );

    param = String::New("fields");
    if (options->Has(param))
    {
        Local<Value> param_val = options->Get(param);
        if (!param_val->IsArray())
          return ThrowException(Exception::TypeError(
            String::New("'fields' must be an array of strings")));
        Local<Array> a = Local<Array>::Cast(param_val);
        uint32_t i = 0;
        closure->num_fields = a->Length();
        while (i < closure->num_fields) {
            Local<Value> name = a->Get(i);
            if (name->IsString()){
                closure->grid_ptr->add_property_name(TOSTR(name));
            }
            i++;
        }
    }

    eio_custom(EIO_RenderGrid, EIO_PRI_DEFAULT, EIO_AfterRenderGrid, closure);
    ev_ref(EV_DEFAULT_UC);
    m->Ref();
    return Undefined();

}


int Map::EIO_RenderGrid(eio_req *req)
{

    grid_t *closure = static_cast<grid_t *>(req->data);

    std::vector<mapnik::layer> const& layers = closure->m->map_->layers();
    
    if (!closure->layer_name.empty()) {
        bool found = false;
        unsigned int idx(0);
        std::string const & layer_name = closure->layer_name;
        BOOST_FOREACH ( mapnik::layer const& lyr, layers )
        {
            if (lyr.name() == layer_name)
            {
                found = true;
                closure->layer_idx = idx;
                break;
            }
            ++idx; 
        }
        if (!found) 
        {
            std::ostringstream s;
            s << "Layer name '" << layer_name << "' not found";
            closure->error = true;
            closure->error_name = s.str();
            return 0;
        }
    }
    else 
    {
        std::size_t layer_num = layers.size();
        std::size_t layer_idx = closure->layer_idx;
    
        if (layer_idx >= layer_num) {
            std::ostringstream s;
            s << "Zero-based layer index '" << layer_idx << "' not valid, only '"
              << layers.size() << "' layers are in map";
            closure->error = true;
            closure->error_name = s.str();
            return 0;
        }    
    }

    // copy property names
    std::set<std::string> attributes = closure->grid_ptr->property_names();

    std::string const& join_field = closure->join_field;
    
    if (join_field == closure->grid_ptr->id_name_) 
    {
        // TODO - should feature.id() be a first class attribute?
        if (attributes.find(join_field) != attributes.end())
        {
            attributes.erase(join_field);
        }
    }
    else if (attributes.find(join_field) == attributes.end())
    {
        attributes.insert(join_field);
    }

    try
    {
        mapnik::grid_renderer<mapnik::grid> ren(*closure->m->map_,*closure->grid_ptr,1.0,0,0);
        mapnik::layer const& layer = layers[closure->layer_idx];
        ren.apply(layer,attributes);
    
    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "Unknown error occured, please file bug";
    }

    return 0;

}

void grid2utf(mapnik::grid const& grid, 
    Local<Array>& l,
    std::vector<mapnik::grid::lookup_type>& key_order)
{
    mapnik::grid::data_type const& data = grid.data();
    mapnik::grid::feature_key_type const& feature_keys = grid.get_feature_keys();
    mapnik::grid::key_type keys;
    mapnik::grid::key_type::const_iterator key_pos;
    mapnik::grid::feature_key_type::const_iterator feature_pos;
    uint16_t codepoint = 31;
    uint16_t row_idx = 0;

    for (unsigned y = 0; y < data.height(); ++y)
    {
        uint16_t idx = 0;
        boost::scoped_array<uint16_t> line(new uint16_t[data.width()]);
        mapnik::grid::value_type const* row = data.getRow(y);
        for (unsigned x = 0; x < data.width(); ++x)
        {
            feature_pos = feature_keys.find(row[x]);
            if (feature_pos != feature_keys.end())
            {
                mapnik::grid::lookup_type const& val = feature_pos->second;
                key_pos = keys.find(val);
                if (key_pos == keys.end())
                {
                    // Create a new entry for this key. Skip the codepoints that
                    // can't be encoded directly in JSON.
                    ++codepoint;
                    if (codepoint == 34) ++codepoint;      // Skip "
                    else if (codepoint == 92) ++codepoint; // Skip backslash
                
                    keys[val] = codepoint;
                    key_order.push_back(val);
                    line[idx++] = static_cast<uint16_t>(codepoint);
                }
                else
                {
                    line[idx++] = static_cast<uint16_t>(key_pos->second);
                }
            }
            // else, shouldn't get here...
        }
        l->Set(row_idx, String::New(line.get(),data.width()));
        ++row_idx;
    }
}


void write_features(mapnik::grid::feature_type const& g_features,
    Local<Object>& feature_data,
    std::vector<mapnik::grid::lookup_type> const& key_order,
    std::string const& join_field,
    std::set<std::string> const& attributes)
{
    mapnik::grid::feature_type::const_iterator feat_itr = g_features.begin();
    mapnik::grid::feature_type::const_iterator feat_end = g_features.end();
    bool include_join_field = (attributes.find(join_field) != attributes.end());
    for (; feat_itr != feat_end; ++feat_itr)
    {
        std::map<std::string,mapnik::value> const& props = feat_itr->second;
        std::map<std::string,mapnik::value>::const_iterator const& itr = props.find(join_field);
        if (itr != props.end())
        {
            mapnik::grid::lookup_type const& join_value = itr->second.to_string();
    
            // only serialize features visible in the grid
            if(std::find(key_order.begin(), key_order.end(), join_value) != key_order.end()) {
                Local<Object> feat = Object::New();
                std::map<std::string,mapnik::value>::const_iterator it = props.begin();
                std::map<std::string,mapnik::value>::const_iterator end = props.end();
                bool found = false;
                for (; it != end; ++it)
                {
                    std::string const& key = it->first;
                    if (key == join_field) {
                        // drop join_field unless requested
                        if (include_join_field) {
                            found = true;
                            params_to_object serializer( feat , it->first);
                            boost::apply_visitor( serializer, it->second.base() );
                        }
                    }
                    else if ( (attributes.find(key) != attributes.end()) )
                    {
                        found = true;
                        params_to_object serializer( feat , it->first);
                        boost::apply_visitor( serializer, it->second.base() );
                    }
                }
                if (found)
                {
                    feature_data->Set(String::NewSymbol(feat_itr->first.c_str()), feat);
                }
            }
        }
        else
        {
            std::clog << "should not get here: join_field '" << join_field << "' not found in grid feature properties\n";
        }
    }
}

int Map::EIO_AfterRenderGrid(eio_req *req)
{
    HandleScope scope;

    grid_t *closure = static_cast<grid_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        // TODO - add more attributes
        // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Error
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        // convert buffer to utf and gather key order
        Local<Array> grid_array = Array::New();
        std::vector<mapnik::grid::lookup_type> key_order;
        grid2utf(*closure->grid_ptr,grid_array,key_order);
    
        // convert key order to proper javascript array
        Local<Array> keys_a = Array::New(key_order.size());
        std::vector<std::string>::iterator it;
        unsigned int i;
        for (it = key_order.begin(), i = 0; it < key_order.end(); ++it, ++i)
        {
            keys_a->Set(i, String::New((*it).c_str()));
        }
    
        // gather feature data
        Local<Object> feature_data = Object::New();
        if (closure->num_fields > 0) {
            mapnik::grid::feature_type const& g_features = closure->grid_ptr->get_grid_features();
            write_features(g_features,
                           feature_data,
                           key_order,
                           closure->join_field,
                           closure->grid_ptr->property_names());
        }
        
        // Create the return hash.
        Local<Object> json = Object::New();
        json->Set(String::NewSymbol("grid"), grid_array);
        json->Set(String::NewSymbol("keys"), keys_a);
        json->Set(String::NewSymbol("data"), feature_data);
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(json) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    
    return 0;
}

#else



// Old, local implementation


struct grid_t {
    Map *m;
    std::size_t layer_idx;
    unsigned int step;
    std::string join_field;
    uint16_t* grid;
    int grid_length;
    bool error;
    bool include_features;
    bool grid_initialized;
    std::set<std::string> property_names;
    std::string error_name;
    std::map<std::string, UChar> keys;
    std::map<std::string, std::map<std::string, mapnik::value> > features;
    std::vector<std::string> key_order;
    Persistent<Function> cb;

    grid_t() : m(NULL), grid(NULL) {
    }

    ~grid_t() {
        if (grid) {
            delete[] grid;
            grid = NULL;
        }
    }
};

void grid2utf(agg_grid::grid_rendering_buffer& renbuf, 
    grid_t* closure,
    std::map<agg_grid::grid_value, std::string>& feature_keys)
{
    uint16_t codepoint = 31;
    int32_t index = 0;

    closure->grid[index++] = (uint16_t)'[';

    std::map<std::string, UChar>::const_iterator key_pos;
    std::map<agg_grid::grid_value, std::string>::const_iterator feature_pos;

    for (unsigned y = 0; y < renbuf.height(); ++y)
    {
        closure->grid[index++] = (uint16_t)'"';
        agg_grid::grid_value* row = renbuf.row(y);
        for (unsigned x = 0; x < renbuf.width(); ++x)
        {
            agg_grid::grid_value id = row[x];
  
            feature_pos = feature_keys.find(id);
            if (feature_pos != feature_keys.end())
            {
                std::string val = feature_pos->second;
                key_pos = closure->keys.find(val);
                if (key_pos == closure->keys.end())
                {
                    // Create a new entry for this key. Skip the codepoints that
                    // can't be encoded directly in JSON.
                    ++codepoint;
                    if (codepoint == 34) ++codepoint;      // Skip "
                    else if (codepoint == 92) ++codepoint; // Skip backslash
                
                    closure->keys[val] = codepoint;
                    closure->key_order.push_back(val);
                    closure->grid[index++] = codepoint;
                }
                else
                {
                    closure->grid[index++] = (uint16_t)key_pos->second;
                }
    
            }
            // else, shouldn't get here...
        }
        closure->grid[index++] = (uint16_t)'"';
        closure->grid[index++] = (uint16_t)',';
    }
    closure->grid[index - 1] = (uint16_t)']';
}

Handle<Value> Map::render_grid(const Arguments& args)
{
    HandleScope scope;

    if (!args.Length() >= 4)
      return ThrowException(Exception::Error(
        String::New("please provide layer idx, step, join_field, field_names, and callback")));

    if ((!args[0]->IsNumber() || !args[1]->IsNumber()))
        return ThrowException(Exception::TypeError(
           String::New("layer idx and step must be integers")));

    if ((!args[2]->IsString()))
        return ThrowException(Exception::TypeError(
           String::New("layer join_field must be a string")));

    bool include_features = false;

    if ((args.Length() > 4)) {
        if (!args[3]->IsBoolean())
            return ThrowException(Exception::TypeError(
               String::New("option to include feature data must be a boolean")));
        include_features = args[3]->BooleanValue();
    }

    std::set<std::string> property_names;
    if ((args.Length() > 5)) {
        if (!args[4]->IsArray())
            return ThrowException(Exception::TypeError(
               String::New("option to restrict to certain field names must be an array")));
        Local<Array> a = Local<Array>::Cast(args[4]);

        uint32_t i = 0;
        uint32_t a_length = a->Length();
        while (i < a_length) {
            Local<Value> name = a->Get(i);
            if (name->IsString()){
                property_names.insert(TOSTR(name));
            }
            i++;
        }
    }
    
    // function callback
    if (!args[args.Length()-1]->IsFunction())
        return ThrowException(Exception::TypeError(
                  String::New("last argument must be a callback function")));

    grid_t *closure = new grid_t();

    if (!closure) {
        V8::LowMemoryNotification();
        return ThrowException(Exception::Error(
            String::New("Could not allocate enough memory")));
    }

    Map* m = ObjectWrap::Unwrap<Map>(args.This());
    
    unsigned int w = m->map_->width();
    
    if (!(w == m->map_->height()))
    {
        return ThrowException(Exception::Error(
                  String::New("Map width and height do not match, this function requires a square tile")));
    }
    
    // http://graphics.stanford.edu/~seander/bithacks.html#DetermineIfPowerOf2
    if (!(w && !(w & (w - 1)))) {
        return ThrowException(Exception::Error(
            String::New("Map width and height must be a power of two")));    
    }

    closure->m = m;
    closure->layer_idx = static_cast<std::size_t>(args[0]->NumberValue());
    closure->step = args[1]->NumberValue();
    closure->join_field = TOSTR(args[2]);
    closure->include_features = include_features;
    closure->property_names = property_names;
    closure->error = false;
    closure->grid_initialized = false;
    closure->cb = Persistent<Function>::New(Handle<Function>::Cast(args[args.Length()-1]));

    // The exact string length:
    //   +3: length + two quotes and a comma
    //   +1: we don't need the last comma, but we need [ and ]
    unsigned int width = w/closure->step;
    closure->grid_length = width * (width + 3) + 1;
    closure->grid = new uint16_t[closure->grid_length];

    eio_custom(EIO_RenderGrid, EIO_PRI_DEFAULT, EIO_AfterRenderGrid, closure);
    ev_ref(EV_DEFAULT_UC);
    m->Ref();
    return Undefined();

}


struct rasterizer :  agg_grid::grid_rasterizer, boost::noncopyable {};

int Map::EIO_RenderGrid(eio_req *req)
{

    grid_t *closure = static_cast<grid_t *>(req->data);

    std::vector<mapnik::layer> const& layers = closure->m->map_->layers();
    std::size_t layer_num = layers.size();
    unsigned int layer_idx = closure->layer_idx;

    if (layer_idx >= layer_num) {
        std::ostringstream s;
        s << "Zero-based layer index '" << layer_idx << "' not valid, only '"
          << layers.size() << "' layers are in map";
        closure->error = true;
        closure->error_name = s.str();
        return 0;
    }

    mapnik::layer const& layer = layers[layer_idx];

    mapnik::datasource_ptr ds = layer.datasource();
    
    if (!ds) {
        closure->error = true;
        closure->error_name = "Layer does not have a Datasource";
        return 0;
    
    }
    
    if (ds->type() == mapnik::datasource::Raster ) {
        closure->error = true;
        closure->error_name = "Raster layers are not yet supported";
        return 0;
    }

    unsigned int step = closure->step;
    std::string join_field = closure->join_field;

    unsigned int width = closure->m->map_->width()/step;
    unsigned int height = closure->m->map_->height()/step;
    
    const mapnik::box2d<double>&  ext = closure->m->map_->get_current_extent();
    //const mapnik::box2d<double>&  ext = closure->m->map_->get_buffered_extent();
    mapnik::CoordTransform tr = mapnik::CoordTransform(width,height,ext);

    try
    {

        //double z = 0;
        mapnik::projection proj0(closure->m->map_->srs());
        mapnik::projection proj1(layer.srs());
        mapnik::proj_transform prj_trans(proj0,proj1);


        mapnik::box2d<double> layer_ext = layer.envelope();
           
        double lx0 = layer_ext.minx();
        double ly0 = layer_ext.miny();
        double lz0 = 0.0;
        double lx1 = layer_ext.maxx();
        double ly1 = layer_ext.maxy();
        double lz1 = 0.0;
        prj_trans.backward(lx0,ly0,lz0);
        prj_trans.backward(lx1,ly1,lz1);
        if ( lx0 > ext.maxx() || lx1 < ext.minx() || ly0 > ext.maxy() || ly1 < ext.miny() )
        {
            //closure->error = true;
            //closure->error_name = "Layer does not interesect with map";
            //std::clog << "bbox of " << layer.name() << " does not intersect map, skipping\n";
            return 0;
        }
        
        // clip query bbox
        lx0 = std::max(ext.minx(),lx0);
        ly0 = std::max(ext.miny(),ly0);
        lx1 = std::min(ext.maxx(),lx1);
        ly1 = std::min(ext.maxy(),ly1);
        
        prj_trans.forward(lx0,ly0,lz0);
        prj_trans.forward(lx1,ly1,lz1);
        mapnik::box2d<double> bbox(lx0,ly0,lx1,ly1);

        #if MAPNIK_VERSION >= 800
            mapnik::query q(bbox);
        #else
            mapnik::query q(bbox,1.0,1.0);
        #endif
        
        if (closure->include_features) {
            bool matched_join_field = false;
            mapnik::layer_descriptor ld = ds->get_descriptor();
            std::vector<mapnik::attribute_descriptor> const& desc = ld.get_descriptors();
            std::vector<mapnik::attribute_descriptor>::const_iterator itr = desc.begin();
            std::vector<mapnik::attribute_descriptor>::const_iterator end = desc.end();
            unsigned size=0;
            bool selected_attr = !closure->property_names.empty();
            bool has_attributes = false;
            while (itr != end)
            {
                has_attributes = true; // memory_datasource will not report fields because it does not know them
                std::string name = itr->get_name();
                if (name == join_field) {
                    matched_join_field = true;
                    q.add_property_name(name);
                }
                else if (selected_attr) {
                    bool requested = (closure->property_names.find(name) != closure->property_names.end());
                    if (requested) {
                        q.add_property_name(name);
                    }
                }
                else {
                    q.add_property_name(name);
                }
                ++itr;
                ++size;
            }
            if (!matched_join_field) {
                if (!has_attributes) {
                    q.add_property_name(join_field);
                } else {
                    closure->error = true;
                    std::ostringstream s("");
                    s << "join_field: '" << join_field << "' is not a valid attribute name";
                    s << "\nValid fields are:";
                    itr = desc.begin();
                    end = desc.end();
                    while (itr != end)
                    {
                        s << " " << itr->get_name();
                        ++itr;
                    }
                    closure->error_name = s.str();
                    return 0;
                }
            }

        }
        else
        {
            q.add_property_name(join_field);
        }

        mapnik::featureset_ptr fs = ds->features(q);
        typedef mapnik::coord_transform2<mapnik::CoordTransform,mapnik::geometry_type> path_type;
    
        agg_grid::grid_value feature_id = 1;
        std::map<agg_grid::grid_value, std::string> feature_keys;
        std::map<agg_grid::grid_value, std::string>::const_iterator feature_pos;
        
        if (fs)
        {
            
            grid_buffer pixmap_(width,height);
            agg_grid::grid_rendering_buffer renbuf(pixmap_.getData(), width, height, width);
            agg_grid::grid_renderer<agg_grid::span_grid> ren_grid(renbuf);
            rasterizer ras_grid;
            
            agg_grid::grid_value no_hit = 0;
            std::string no_hit_val = "";
            feature_keys[no_hit] = no_hit_val;
            ren_grid.clear(no_hit);
    
            mapnik::feature_ptr feature;
            while ((feature = fs->next()))
            {
                ras_grid.reset();
                ++feature_id;
 
                for (unsigned i=0;i<feature->num_geometries();++i)
                {
                    mapnik::geometry_type const& geom=feature->get_geometry(i);
                    mapnik::eGeomType g_type = geom.type();
                    if (g_type == mapnik::Point)
                    {
    
                        double x;
                        double y;
                        double z=0;
                        int i;
                        geom.label_position(&x, &y);
                        // TODO - check return of proj_trans
                        prj_trans.backward(x,y,z);
                        //bool ok = prj_trans.backward(x,y,z);
                        //if (!ok)
                        //    std::clog << "warning proj_trans failed\n";
                        tr.forward(&x,&y);
                        //if (x < 0 || y < 0)
                        //    std::clog << "warning: invalid point values being rendered: " << x << " " << y << "\n";
                        int approximation_steps = 360;
                        int rx = 10/step; // arbitary pixel width
                        int ry = 10/step; // arbitary pixel height
                        ras_grid.move_to_d(x + rx, y);
                        for(i = 1; i < approximation_steps; i++)
                        {
                            double a = double(i) * 3.1415926 / 180.0;
                            ras_grid.line_to_d(x + cos(a) * rx, y + sin(a) * ry);
                        }
                    }
                    else if (geom.num_points() > 1)
                    {
                        if (g_type == mapnik::LineString) {
                            path_type path(tr,geom,prj_trans);
                            agg::conv_stroke<path_type>  stroke(path);
                            ras_grid.add_path(stroke);
                        }
                        else {
                            path_type path(tr,geom,prj_trans);
                            ras_grid.add_path(path);
                        }
                    }
                }
    
                std::string val = "";
                std::map<std::string,mapnik::value> const& fprops = feature->props();
                std::map<std::string,mapnik::value>::const_iterator const& itr = fprops.find(join_field);
                if (itr != fprops.end())
                {
                    val = itr->second.to_string();
                }
    
                feature_pos = feature_keys.find(feature_id);
                if (feature_pos == feature_keys.end())
                {
                    feature_keys[feature_id] = val;
                    if (closure->include_features && (val != ""))
                        closure->features[val] = fprops;
                }
                
                ras_grid.render(ren_grid, feature_id);
    
            }
            
            // resample and utf-ize the grid
            closure->grid_initialized = true;
            grid2utf(renbuf, closure, feature_keys);
        }

    }
    catch (const mapnik::config_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::datasource_exception & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::proj_init_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::runtime_error & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const mapnik::ImageWriterException & ex )
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (const std::exception & ex)
    {
        closure->error = true;
        closure->error_name = ex.what();
    }
    catch (...)
    {
        closure->error = true;
        closure->error_name = "Unknown error occured, please file bug";
    }

    return 0;

}


int Map::EIO_AfterRenderGrid(eio_req *req)
{
    HandleScope scope;

    grid_t *closure = static_cast<grid_t *>(req->data);
    ev_unref(EV_DEFAULT_UC);

    TryCatch try_catch;

    if (closure->error) {
        // TODO - add more attributes
        // https://developer.mozilla.org/en/JavaScript/Reference/Global_Objects/Error
        Local<Value> argv[1] = { Exception::Error(String::New(closure->error_name.c_str())) };
        closure->cb->Call(Context::GetCurrent()->Global(), 1, argv);
    } else {
        Local<Array> keys_a = Array::New(closure->keys.size());
        std::vector<std::string>::iterator it;
        unsigned int i;
        for (it = closure->key_order.begin(), i = 0; it < closure->key_order.end(); ++it, ++i)
        {
            keys_a->Set(i, String::New((*it).c_str()));
        }

        Local<Object> data = Object::New();
        if (closure->include_features) {
            typedef std::map<std::string, std::map<std::string, mapnik::value> >::const_iterator const_style_iterator;
            const_style_iterator feat_itr = closure->features.begin();
            const_style_iterator feat_end = closure->features.end();
            for (; feat_itr != feat_end; ++feat_itr)
            {
                std::map<std::string,mapnik::value> props = feat_itr->second;
                std::string join_value = props[closure->join_field].to_string();
                // only serialize features visible in the grid
                if(std::find(closure->key_order.begin(), closure->key_order.end(), join_value) != closure->key_order.end()) {
                    Local<Object> feat = Object::New();
                    std::map<std::string,mapnik::value>::const_iterator it = props.begin();
                    std::map<std::string,mapnik::value>::const_iterator end = props.end();
                    for (; it != end; ++it)
                    {
                        params_to_object serializer( feat , it->first);
                        // need to call base() since this is a mapnik::value
                        // not a mapnik::value_holder
                        boost::apply_visitor( serializer, it->second.base() );
                    }
                    data->Set(String::NewSymbol(feat_itr->first.c_str()), feat);
                }
            }
        }
        
        // Create the return hash.
        Local<Object> json = Object::New();
        if (closure->grid_initialized) {
            json->Set(String::NewSymbol("grid"), String::New(closure->grid, closure->grid_length));
        }
        else {
            json->Set(String::NewSymbol("grid"), Array::New());
        }
        json->Set(String::NewSymbol("keys"), keys_a);
        json->Set(String::NewSymbol("data"), data);
        Local<Value> argv[2] = { Local<Value>::New(Null()), Local<Value>::New(json) };
        closure->cb->Call(Context::GetCurrent()->Global(), 2, argv);
    }

    if (try_catch.HasCaught()) {
      FatalException(try_catch);
    }

    closure->m->Unref();
    closure->cb.Dispose();
    delete closure;
    
    return 0;
}

#endif
