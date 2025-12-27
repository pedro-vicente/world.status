#include <Wt/WApplication.h>
#include <Wt/WContainerWidget.h>
#include <Wt/WCompositeWidget.h>
#include <Wt/WHBoxLayout.h>
#include <Wt/WText.h>
#include <Wt/WVBoxLayout.h>
#include <Wt/WCheckBox.h>
#include <Wt/WPushButton.h>
#include <Wt/WBreak.h>
#include "duckdb.hpp"
#ifdef _WIN32
#include <windows.h>
#endif
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <fstream>

/////////////////////////////////////////////////////////////////////////////////////////////////////
// Country
/////////////////////////////////////////////////////////////////////////////////////////////////////

struct Country
{
  std::string cca2;
  std::string name;
  std::string geojson;
  double area;

  Country(const std::string& code, const std::string& n, const std::string& geo, double a)
    : cca2(code), name(n), geojson(geo), area(a) {
  }
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
// globals
/////////////////////////////////////////////////////////////////////////////////////////////////////

std::string db_file = "world.duckdb";
std::vector<Country> countries;
std::map<std::string, std::string> continent_colors;

/////////////////////////////////////////////////////////////////////////////////////////////////////
// load_countries
/////////////////////////////////////////////////////////////////////////////////////////////////////

int load_countries()
{
  try
  {
    duckdb::DuckDB db(db_file);
    duckdb::Connection conn(db);

    duckdb::unique_ptr<duckdb::MaterializedQueryResult> result = conn.Query("INSTALL spatial");
    result = conn.Query("LOAD spatial");
    if (result->HasError())
    {
      return -1;
    }

    std::string sql = "SELECT UPPER(cca2), name, ST_AsGeoJSON(geom), ST_Area(geom) FROM countries ORDER BY name";

    result = conn.Query(sql);
    if (result->HasError())
    {
      return -1;
    }

    size_t row_count = result->RowCount();
    for (size_t idx = 0; idx < row_count; ++idx)
    {
      std::string cca2 = result->GetValue(0, idx).ToString();
      std::string name = result->GetValue(1, idx).ToString();
      std::string geojson = result->GetValue(2, idx).ToString();
      double area = 0.0;
      try
      {
        area = result->GetValue(3, idx).GetValue<double>();
      }
      catch (...) {}

      if (!cca2.empty() && !geojson.empty())
      {
        countries.push_back(Country(cca2, name, geojson, area));
      }
    }

    return 1;
  }
  catch (const std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return -1;
  }
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// escape_js_string
/////////////////////////////////////////////////////////////////////////////////////////////////////

std::string escape_js_string(const std::string& input)
{
  std::string output;
  output.reserve(input.size());
  for (size_t idx = 0; idx < input.size(); ++idx)
  {
    char c = input[idx];
    switch (c)
    {
    case '\'': output += "\\'"; break;
    case '\"': output += "\\\""; break;
    case '\\': output += "\\\\"; break;
    case '\n': output += "\\n"; break;
    case '\r': output += "\\r"; break;
    case '\t': output += "\\t"; break;
    default: output += c; break;
    }
  }
  return output;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// rgb_to_hex
/////////////////////////////////////////////////////////////////////////////////////////////////////

std::string rgb_to_hex(int r, int g, int b)
{
  char hex[8];
  snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
  return std::string(hex);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// WMapLibre
/////////////////////////////////////////////////////////////////////////////////////////////////////

namespace Wt
{
  class WT_API WMapLibre : public WCompositeWidget
  {
    class Impl;
  public:
    WMapLibre();
    ~WMapLibre();

    std::vector<Country>* countries_ptr;

  protected:
    Impl* impl;
    virtual void render(WFlags<RenderFlag> flags) override;
  };

  class WMapLibre::Impl : public WWebWidget
  {
  public:
    Impl();
    virtual DomElementType domElementType() const override;
  };

  WMapLibre::Impl::Impl()
  {
    setInline(false);
  }

  DomElementType WMapLibre::Impl::domElementType() const
  {
    return DomElementType::DIV;
  }

  WMapLibre::WMapLibre()
    : countries_ptr(nullptr)
  {
    setImplementation(std::unique_ptr<Impl>(impl = new Impl()));
    WApplication* app = WApplication::instance();
    this->addCssRule("body", "margin: 0; padding: 0;");
    this->addCssRule("#" + id(), "position: absolute; top: 0; bottom: 0; width: 100%;");
    app->useStyleSheet("https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.css");
    const std::string library = "https://unpkg.com/maplibre-gl@4.7.1/dist/maplibre-gl.js";
    app->require(library, "maplibre");
  }

  WMapLibre::~WMapLibre()
  {
  }

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // render
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  void WMapLibre::render(WFlags<RenderFlag> flags)
  {
    WCompositeWidget::render(flags);

    if (flags.test(RenderFlag::Full))
    {
      std::stringstream js;

      /////////////////////////////////////////////////////////////////////////////////////////////////////
      // create map
      /////////////////////////////////////////////////////////////////////////////////////////////////////

      js << "const map = new maplibregl.Map({\n"
        << "  container: " << jsRef() << ",\n"
        << "  style: 'https://basemaps.cartocdn.com/gl/positron-gl-style/style.json',\n"
        << "  center: [0, 20],\n"
        << "  zoom: 2\n"
        << "});\n"
        << "map.addControl(new maplibregl.NavigationControl());\n";

      js << "map.on('load', function() {\n";

      /////////////////////////////////////////////////////////////////////////////////////////////////////
      // add countries as a single FeatureCollection
      /////////////////////////////////////////////////////////////////////////////////////////////////////

      if (countries_ptr && !countries_ptr->empty())
      {
        js << "var countriesData = {\n"
          << "  'type': 'FeatureCollection',\n"
          << "  'features': [\n";

        for (size_t idx = 0; idx < countries_ptr->size(); ++idx)
        {
          const Country& country = (*countries_ptr)[idx];

          js << "    {\n"
            << "      'type': 'Feature',\n"
            << "      'properties': {\n"
            << "        'cca2': '" << country.cca2 << "',\n"
            << "        'name': '" << escape_js_string(country.name) << "',\n"
            << "        'area': " << country.area << "\n"
            << "      },\n"
            << "      'geometry': " << country.geojson << "\n"
            << "    }";

          if (idx < countries_ptr->size() - 1) js << ",";
          js << "\n";
        }

        js << "  ]\n"
          << "};\n";

        /////////////////////////////////////////////////////////////////////////////////////////////////////
        // add source and layers
        /////////////////////////////////////////////////////////////////////////////////////////////////////

        js << "map.addSource('countries', {\n"
          << "  'type': 'geojson',\n"
          << "  'data': countriesData\n"
          << "});\n";

        js << "map.addLayer({\n"
          << "  'id': 'countries-fill',\n"
          << "  'type': 'fill',\n"
          << "  'source': 'countries',\n"
          << "  'paint': {\n"
          << "    'fill-color': [\n"
          << "      'interpolate',\n"
          << "      ['linear'],\n"
          << "      ['get', 'area'],\n"
          << "      0, '#ffffcc',\n"
          << "      100, '#c7e9b4',\n"
          << "      1000, '#7fcdbb',\n"
          << "      5000, '#41b6c4',\n"
          << "      10000, '#1d91c0',\n"
          << "      50000, '#225ea8',\n"
          << "      100000, '#0c2c84'\n"
          << "    ],\n"
          << "    'fill-opacity': 0.6\n"
          << "  }\n"
          << "});\n";

        js << "map.addLayer({\n"
          << "  'id': 'countries-border',\n"
          << "  'type': 'line',\n"
          << "  'source': 'countries',\n"
          << "  'paint': {\n"
          << "    'line-color': '#333',\n"
          << "    'line-width': 1\n"
          << "  }\n"
          << "});\n";

        /////////////////////////////////////////////////////////////////////////////////////////////////////
        // popup on hover
        /////////////////////////////////////////////////////////////////////////////////////////////////////

        js << "var popup = new maplibregl.Popup({\n"
          << "  closeButton: false,\n"
          << "  closeOnClick: false\n"
          << "});\n";

        js << "map.on('mousemove', 'countries-fill', function(e) {\n"
          << "  map.getCanvas().style.cursor = 'pointer';\n"
          << "  var props = e.features[0].properties;\n"
          << "  var html = '<strong>' + props.name + '</strong><br>'"
          << "    + 'Code: ' + props.cca2 + '<br>'"
          << "    + 'Area: ' + props.area.toFixed(2);\n"
          << "  popup.setLngLat(e.lngLat).setHTML(html).addTo(map);\n"
          << "});\n";

        js << "map.on('mouseleave', 'countries-fill', function() {\n"
          << "  map.getCanvas().style.cursor = '';\n"
          << "  popup.remove();\n"
          << "});\n";

        /////////////////////////////////////////////////////////////////////////////////////////////////////
        // click to zoom
        /////////////////////////////////////////////////////////////////////////////////////////////////////

        js << "map.on('click', 'countries-fill', function(e) {\n"
          << "  var bbox = turf.bbox(e.features[0]);\n"
          << "  map.fitBounds(bbox, { padding: 50 });\n"
          << "});\n";
      }

      // close map.on('load')
      js << "});\n";

      WApplication* app = WApplication::instance();

      app->require("https://unpkg.com/@turf/turf@6/turf.min.js", "turf");

      app->doJavaScript(js.str());
    }
  }

} // namespace Wt

/////////////////////////////////////////////////////////////////////////////////////////////////////
// ApplicationWorldMap
/////////////////////////////////////////////////////////////////////////////////////////////////////

class ApplicationWorldMap : public Wt::WApplication
{
public:
  ApplicationWorldMap(const Wt::WEnvironment& env);

private:
  Wt::WMapLibre* map;
  Wt::WContainerWidget* map_container;
  Wt::WText* info_text;
};

/////////////////////////////////////////////////////////////////////////////////////////////////////
// ApplicationWorldMap
/////////////////////////////////////////////////////////////////////////////////////////////////////

ApplicationWorldMap::ApplicationWorldMap(const Wt::WEnvironment& env)
  : WApplication(env), map(nullptr), map_container(nullptr), info_text(nullptr)
{
  setTitle("World Countries Map");

  std::unique_ptr<Wt::WHBoxLayout> layout = std::make_unique<Wt::WHBoxLayout>();
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // sidebar
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  std::unique_ptr<Wt::WContainerWidget> sidebar = std::make_unique<Wt::WContainerWidget>();
  sidebar->setPadding(10);
  sidebar->setWidth(250);
  std::unique_ptr<Wt::WVBoxLayout> layout_sidebar = std::make_unique<Wt::WVBoxLayout>();

  layout_sidebar->addWidget(std::make_unique<Wt::WText>("<h3>World Countries</h3>"));

  layout_sidebar->addWidget(std::make_unique<Wt::WText>("<h4>Legend</h4>"));

  std::string legend =
    "<div style='font-size:11px;'>"
    "<div><span style='background:#ffffcc;padding:2px 8px;'></span> &lt; 100</div>"
    "<div><span style='background:#c7e9b4;padding:2px 8px;'></span> 100-1000</div>"
    "<div><span style='background:#7fcdbb;padding:2px 8px;'></span> 1000-5000</div>"
    "<div><span style='background:#41b6c4;padding:2px 8px;'></span> 5000-10000</div>"
    "<div><span style='background:#1d91c0;padding:2px 8px;'></span> 10000-50000</div>"
    "<div><span style='background:#225ea8;padding:2px 8px;'></span> 50000-100000</div>"
    "<div><span style='background:#0c2c84;padding:2px 8px;'></span> &gt; 100000</div>"
    "</div>";

  layout_sidebar->addWidget(std::make_unique<Wt::WText>(legend));

  layout_sidebar->addStretch(1);
  sidebar->setLayout(std::move(layout_sidebar));
  layout->addWidget(std::move(sidebar), 0);

  /////////////////////////////////////////////////////////////////////////////////////////////////////
  // map
  /////////////////////////////////////////////////////////////////////////////////////////////////////

  std::unique_ptr<Wt::WContainerWidget> container_map = std::make_unique<Wt::WContainerWidget>();
  map_container = container_map.get();
  map = container_map->addWidget(std::make_unique<Wt::WMapLibre>());
  map->resize(Wt::WLength::Auto, Wt::WLength::Auto);
  map->countries_ptr = &countries;

  layout->addWidget(std::move(container_map), 1);
  root()->setLayout(std::move(layout));
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// create_application
/////////////////////////////////////////////////////////////////////////////////////////////////////

std::unique_ptr<Wt::WApplication> create_application(const Wt::WEnvironment& env)
{
  return std::make_unique<ApplicationWorldMap>(env);
}

/////////////////////////////////////////////////////////////////////////////////////////////////////
// main
/////////////////////////////////////////////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
  if (load_countries() < 0)
  {
    return 1;
  }

  return Wt::WRun(argc, argv, &create_application);
}
