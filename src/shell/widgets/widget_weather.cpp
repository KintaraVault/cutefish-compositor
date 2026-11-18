// SPDX-License-Identifier: BSD-3-Clause
#include "shell/widgets/widget_weather.h"

#include <charconv>
#include <cmath>
#include <memory>

#include <curl/curl.h>

#include "base/json.h"

namespace cutefish::shell::widget_weather {

std::optional<Coordinates> coordinates(std::string_view input)
{
    const auto number = [](std::string_view part, double limit) -> std::optional<double> {
        const auto first = part.find_first_not_of(" \t");
        if (first == part.npos)
            return {};
        part = part.substr(first, part.find_last_not_of(" \t") - first + 1);
        double value = 0;
        const auto end = part.data() + part.size();
        const auto result = std::from_chars(part.data(), end, value);
        if (result.ec != std::errc() || result.ptr != end || !std::isfinite(value) || std::abs(value) > limit)
            return {};
        return value;
    };
    const auto comma = input.find(',');
    if (comma == input.npos)
        return {};
    const auto lat = number(input.substr(0, comma), 90), lon = number(input.substr(comma + 1), 180);
    return lat && lon ? std::optional(Coordinates {*lat, *lon}) : std::nullopt;
}

std::optional<Forecast> parse(std::string_view json)
{
    const auto data = base::json::Value::parse(json);
    if (!data)
        return {};
    const auto &current = (*data)["current"], &hourly = (*data)["hourly"];
    const auto valid = [](const base::json::Value &temperature, const base::json::Value &code) {
        return temperature.isNumber() && std::abs(temperature.toDouble()) <= 150
                && code.isNumber() && code.toDouble() >= 0 && code.toDouble() <= 99;
    };
    if (!valid(current["temperature_2m"], current["weather_code"]))
        return {};
    Forecast result {int(current["temperature_2m"].toInt()), int(current["weather_code"].toInt()), {}};
    for (size_t i = 0; i < result.hours.size(); ++i) {
        const auto time = hourly["time"][i].toString();
        const auto &temperature = hourly["temperature_2m"][i], &code = hourly["weather_code"][i];
        if (time.size() != 16 || !valid(temperature, code))
            return {};
        result.hours[i] = {time.substr(11, 5), int(temperature.toInt()), int(code.toInt())};
    }
    return result;
}

std::optional<Forecast> fetch(Coordinates location)
{
    static const bool initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    if (!initialized)
        return {};
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl)
        return {};
    const auto decimal = [](double value) {
        char buffer[32];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, 3);
        return std::string(buffer, result.ptr);
    };
    const auto url = "https://api.open-meteo.com/v1/forecast?latitude=" + decimal(location.latitude)
            + "&longitude=" + decimal(location.longitude)
            + "&current=temperature_2m,weather_code&hourly=temperature_2m,weather_code&forecast_hours=6&timezone=auto";
    std::string response;
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT_MS, 2500L);
    curl_easy_setopt(curl.get(), CURLOPT_TIMEOUT_MS, 5500L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *context) -> size_t {
        auto &body = *static_cast<std::string *>(context);
        const auto length = size * count;
        if (length > 32 * 1024 - body.size())
            return 0;
        body.append(data, length);
        return length;
    });
    return curl_easy_perform(curl.get()) == CURLE_OK ? parse(response) : std::nullopt;
}

} // namespace cutefish::shell::widget_weather
