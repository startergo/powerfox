# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

import os

CORETEXT_TBD = os.path.join(
    "System",
    "Library",
    "Frameworks",
    "CoreText.framework",
    "Versions",
    "A",
    "CoreText.tbd",
)

LEGACY_CORETEXT = (
    "/System/Library/Frameworks/ApplicationServices.framework/Versions/A/"
    "Frameworks/CoreText.framework/Versions/A/CoreText"
)


def main(output, sdk_path):
    source_path = os.path.join(sdk_path, CORETEXT_TBD)
    with open(source_path, encoding="utf-8") as source:
        contents = source.read()

    marker = "exports:\n"
    if marker not in contents:
        raise ValueError(f"CoreText stub has no exports section: {source_path}")

    directives = [
        f"$ld$install_name$os10.6${LEGACY_CORETEXT}",
        f"$ld$install_name$os10.7${LEGACY_CORETEXT}",
    ]
    missing_directives = [
        directive for directive in directives if directive not in contents
    ]
    if missing_directives:
        symbols = "',\n                       '".join(missing_directives)
        compatibility_exports = (
            "  - targets:         [ x86_64-macos ]\n"
            f"    symbols:         [ '{symbols}' ]\n"
        )
        contents = contents.replace(marker, marker + compatibility_exports, 1)

    output.write(contents)
    return {source_path}
