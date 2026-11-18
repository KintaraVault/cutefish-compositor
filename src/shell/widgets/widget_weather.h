// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace cutefish::shell::widget_weather {

struct Coordinates { double latitude, longitude; };
struct Forecast {
    struct Hour { std::string time; int temperature = 0; int code = 0; };
    int temperature;
    int code;
    std::array<Hour, 6> hours;
};

std::optional<Coordinates> coordinates(std::string_view input);
std::optional<Forecast> parse(std::string_view json);
std::optional<Forecast> fetch(Coordinates location);

} // namespace cutefish::shell::widget_weather
