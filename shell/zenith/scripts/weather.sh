#!/usr/bin/env bash
# Fetches weather from Open-Meteo with caching, transformed to wttr.in-like format.
# Supports automatic IP-based location or manual location via argument.

# Configuration
CACHE_DIR="$HOME/.cache/weather"
mkdir -p "$CACHE_DIR"
MAX_AGE=1800 # 30 minutes

# Get location query from argument
QUERY="${1:-auto}"
# Create a safe filename for the cache based on the query
CACHE_HASH=$(echo "$QUERY" | md5sum | cut -d' ' -f1)
CACHE_FILE="$CACHE_DIR/$CACHE_HASH.json"

fetch_weather() {
    local lat lon city country
    
    if [ "$QUERY" = "auto" ]; then
        # Automatic IP-based detection using ip-api.com (more accurate for some regions)
        local ip_data=$(curl -s --max-time 5 "http://ip-api.com/json/" 2>/dev/null)
        lat=$(echo "$ip_data" | jq -r '.lat // empty' 2>/dev/null)
        lon=$(echo "$ip_data" | jq -r '.lon // empty' 2>/dev/null)
        city=$(echo "$ip_data" | jq -r '.city // empty' 2>/dev/null)
        country=$(echo "$ip_data" | jq -r '.country // empty' 2>/dev/null)
        
        # Fallback to ipinfo.io if ip-api.com fails
        if [ -z "$lat" ]; then
            local ip_info=$(curl -s --max-time 3 "https://ipinfo.io/json/" 2>/dev/null)
            local loc=$(echo "$ip_info" | jq -r '.loc' 2>/dev/null)
            city=$(echo "$ip_info" | jq -r '.city' 2>/dev/null)
            country=$(echo "$ip_info" | jq -r '.country // empty' 2>/dev/null)
            lat=$(echo "$loc" | cut -d',' -f1)
            lon=$(echo "$loc" | cut -d',' -f2)
        fi

        # Last resort fallback to Karachi
        if [ -z "$lat" ]; then
            lat="24.86"
            lon="67.01"
            city="Karachi"
            country="Pakistan"
        fi
    else
        # Geocoding manual location
        local encoded_query=$(echo "$QUERY" | jq -sRr @uri)
        local geo_data=$(curl -s --max-time 5 "https://geocoding-api.open-meteo.com/v1/search?name=$encoded_query&count=1&language=en&format=json")
        
        lat=$(echo "$geo_data" | jq -r '.results[0].latitude // empty')
        lon=$(echo "$geo_data" | jq -r '.results[0].longitude // empty')
        city=$(echo "$geo_data" | jq -r '.results[0].name // empty')
        country=$(echo "$geo_data" | jq -r '.results[0].country // empty')

        if [ -z "$lat" ]; then
            # Fallback to auto if geocoding fails
            QUERY="auto"
            fetch_weather
            return
        fi
    fi

    # Fetch from Open-Meteo with comprehensive fields
    local om_url="https://api.open-meteo.com/v1/forecast?latitude=$lat&longitude=$lon&current=temperature_2m,weather_code,relative_humidity_2m,apparent_temperature,surface_pressure,wind_speed_10m,wind_direction_10m,cloud_cover&daily=weather_code,temperature_2m_max,temperature_2m_min,uv_index_max,precipitation_probability_max&timezone=auto"
    
    if om_data=$(curl -s --max-time 5 "$om_url"); then
        # Transform to wttr.in-like format
        echo "$om_data" | jq --arg city "$city" --arg country "$country" '
            def wmo_to_wwo(code):
                if code == 0 then "113"
                elif code == 1 then "116"
                elif code == 2 then "119"
                elif code == 3 then "122"
                elif (code >= 45 and code <= 48) then "248"
                elif (code >= 51 and code <= 55) then "266"
                elif (code >= 61 and code <= 65) then "296"
                elif (code >= 71 and code <= 77) then "338"
                elif (code >= 80 and code <= 82) then "296"
                elif (code >= 85 and code <= 86) then "338"
                elif (code >= 95 and code <= 99) then "389"
                else "113" end;

            def wmo_to_desc(code):
                if code == 0 then "Clear sky"
                elif code == 1 then "Mainly clear"
                elif code == 2 then "Partly cloudy"
                elif code == 3 then "Overcast"
                elif (code >= 45 and code <= 48) then "Fog"
                elif (code >= 51 and code <= 55) then "Drizzle"
                elif (code >= 61 and code <= 65) then "Rain"
                elif (code >= 71 and code <= 77) then "Snow"
                elif (code >= 80 and code <= 82) then "Rain showers"
                elif (code >= 85 and code <= 86) then "Snow showers"
                elif (code >= 95 and code <= 99) then "Thunderstorm"
                else "Clear" end;
            
            def deg_to_dir(deg):
                if deg == null then "N/A"
                elif deg >= 337.5 or deg < 22.5 then "N"
                elif deg >= 22.5 and deg < 67.5 then "NE"
                elif deg >= 67.5 and deg < 112.5 then "E"
                elif deg >= 112.5 and deg < 157.5 then "SE"
                elif deg >= 157.5 and deg < 202.5 then "S"
                elif deg >= 202.5 and deg < 247.5 then "SW"
                elif deg >= 247.5 and deg < 292.5 then "W"
                elif deg >= 292.5 and deg < 337.5 then "NW"
                else "N" end;

            {
                nearest_area: [{
                    areaName: [{ value: $city }],
                    country: [{ value: $country }]
                }],
                current_condition: [{
                    weatherCode: wmo_to_wwo(.current.weather_code),
                    temp_C: (.current.temperature_2m | round | tostring),
                    FeelsLikeC: (.current.apparent_temperature | round | tostring),
                    humidity: (.current.relative_humidity_2m | tostring),
                    pressure: (.current.surface_pressure | round | tostring),
                    windspeedKmph: (.current.wind_speed_10m | round | tostring),
                    winddir16Point: deg_to_dir(.current.wind_direction_10m),
                    cloudcover: (.current.cloud_cover | tostring),
                    uvIndex: (.daily.uv_index_max[0] | round | tostring),
                    weatherDesc: [{ value: wmo_to_desc(.current.weather_code) }]
                }],
                weather: [
                    range(0; .daily.time | length) as $i |
                    {
                        date: .daily.time[$i],
                        maxtempC: (.daily.temperature_2m_max[$i] | round | tostring),
                        mintempC: (.daily.temperature_2m_min[$i] | round | tostring),
                        uvIndex: (.daily.uv_index_max[$i] | round | tostring),
                        hourly: [
                            { chanceofrain: (.daily.precipitation_probability_max[$i] | tostring) },
                            {}, {}, {},
                            { weatherCode: wmo_to_wwo(.daily.weather_code[$i]) }
                        ]
                    }
                ]
            }
        ' > "$CACHE_FILE.tmp" && mv "$CACHE_FILE.tmp" "$CACHE_FILE"
    fi
}

# Cache logic
if [ -f "$CACHE_FILE" ]; then
    current_time=$(date +%s)
    file_time=$(date +%s -r "$CACHE_FILE")
    age=$((current_time - file_time))

    if [ $age -gt $MAX_AGE ]; then
        fetch_weather
    fi
else
    fetch_weather
fi

# Output results
if [ -s "$CACHE_FILE" ]; then
    cat "$CACHE_FILE"
else
    echo "{}"
fi
