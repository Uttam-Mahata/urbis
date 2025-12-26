#!/bin/bash
# Download OpenStreetMap data for a city area using Overpass API
# Usage: ./download_osm.sh [city_name] [min_lat,min_lon,max_lat,max_lon]

set -e

CITY_NAME="${1:-san_francisco}"
DATA_DIR="$(dirname "$0")/data"
mkdir -p "$DATA_DIR"

# Bounding box: min_lat,min_lon,max_lat,max_lon
# Default: San Francisco Financial District (small area for quick download)
BBOX="${2:-37.788,-122.405,37.795,-122.395}"

echo "=== Downloading OpenStreetMap data ==="
echo "City: $CITY_NAME"
echo "Bounding Box: $BBOX"
echo ""

# Download from Overpass API with properly formatted query
OSM_FILE="$DATA_DIR/${CITY_NAME}_osm.json"

echo "Downloading from Overpass API..."

# Use a simpler query format that's more reliable
curl -s --retry 3 --max-time 120 \
    -d "[out:json][timeout:90];(way[\"building\"]($BBOX);way[\"highway\"]($BBOX);node[\"amenity\"]($BBOX);node[\"shop\"]($BBOX););out body;>;out skel qt;" \
    "https://overpass-api.de/api/interpreter" > "$OSM_FILE"

# Check if download was successful
if [ ! -s "$OSM_FILE" ] || grep -q "error" "$OSM_FILE" 2>/dev/null; then
    echo "Primary download failed, trying alternative..."
    
    # Try with even simpler query - just buildings and roads
    curl -s --retry 3 --max-time 120 \
        -d "[out:json];(way[building]($BBOX);way[highway]($BBOX););out body;>;out skel qt;" \
        "https://overpass-api.de/api/interpreter" > "$OSM_FILE"
fi

if [ ! -s "$OSM_FILE" ]; then
    echo "Error: Failed to download data"
    exit 1
fi

echo "Downloaded OSM data to: $OSM_FILE"
echo "File size: $(du -h "$OSM_FILE" | cut -f1)"

# Convert OSM JSON to GeoJSON using Python
echo ""
echo "Converting to GeoJSON..."

GEOJSON_FILE="$DATA_DIR/${CITY_NAME}.geojson"

python3 - "$OSM_FILE" "$GEOJSON_FILE" << 'PYTHON_SCRIPT'
import json
import sys

if len(sys.argv) < 3:
    print("Usage: script osm_file geojson_file")
    sys.exit(1)

osm_file = sys.argv[1]
geojson_file = sys.argv[2]

try:
    with open(osm_file, 'r') as f:
        osm_data = json.load(f)
except Exception as e:
    print(f"Error reading OSM file: {e}")
    sys.exit(1)

# Build node lookup for coordinate resolution
nodes = {}
for element in osm_data.get('elements', []):
    if element['type'] == 'node':
        nodes[element['id']] = (element.get('lon', 0), element.get('lat', 0))

features = []
feature_id = 1

for element in osm_data.get('elements', []):
    if element['type'] == 'node' and 'tags' in element:
        # Point of Interest
        lon = element.get('lon', 0)
        lat = element.get('lat', 0)
        if lon and lat:
            features.append({
                "type": "Feature",
                "id": feature_id,
                "properties": element.get('tags', {}),
                "geometry": {
                    "type": "Point",
                    "coordinates": [lon, lat]
                }
            })
            feature_id += 1
        
    elif element['type'] == 'way' and 'nodes' in element:
        # Build coordinates from node references
        coords = []
        for node_id in element['nodes']:
            if node_id in nodes:
                coords.append(list(nodes[node_id]))
        
        if len(coords) >= 2:
            tags = element.get('tags', {})
            
            # Determine geometry type
            is_closed = len(coords) >= 4 and coords[0] == coords[-1]
            is_building = 'building' in tags
            is_area = 'area' in tags and tags['area'] == 'yes'
            
            if is_closed and (is_building or is_area):
                # Polygon (building, area)
                geom = {
                    "type": "Polygon",
                    "coordinates": [coords]
                }
            else:
                # LineString (road, path)
                geom = {
                    "type": "LineString",
                    "coordinates": coords
                }
            
            features.append({
                "type": "Feature",
                "id": feature_id,
                "properties": tags,
                "geometry": geom
            })
            feature_id += 1

# Create GeoJSON
geojson = {
    "type": "FeatureCollection",
    "features": features
}

with open(geojson_file, 'w') as f:
    json.dump(geojson, f, indent=2)

# Statistics
points = sum(1 for f in features if f['geometry']['type'] == 'Point')
lines = sum(1 for f in features if f['geometry']['type'] == 'LineString')
polygons = sum(1 for f in features if f['geometry']['type'] == 'Polygon')

print(f"Created GeoJSON file: {geojson_file}")
print(f"Total features: {len(features)}")
print(f"  Points (POIs): {points}")
print(f"  LineStrings (roads): {lines}")
print(f"  Polygons (buildings): {polygons}")
PYTHON_SCRIPT

echo ""
echo "=== Download complete ==="
echo "GeoJSON file ready at: $GEOJSON_FILE"
