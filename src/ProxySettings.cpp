// Copyright © 2026 SculkCatalystMC. All rights reserved.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as published by
// the Free Software Foundation, version 3 of the License.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ProxySettings.hpp"
#include <sculk/reflection/jsonc/reflection.hpp>

namespace sculk {

bool ProxySettings::load() {
    auto load = reflection::jsonc::load_file(
        *this,
        "./proxy_settings.jsonc",
        reflection::builtin_key_formatter::snake_case_formatter
    );
    return load.has_value();
}

void ProxySettings::save() const {
    reflection::jsonc::save_file(
        *this,
        "./proxy_settings.jsonc",
        reflection::builtin_key_formatter::snake_case_formatter
    );
}

} // namespace sculk