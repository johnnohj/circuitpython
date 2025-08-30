# Test variant - standard variant + one additional module
# This is to test if the build system can handle additional modules

include("$(PORT_DIR)/variants/manifest.py")

# Start with all the standard modules
require("collections")
require("collections-defaultdict")
require("functools") 
require("itertools")
require("base64")
require("os")
require("os-path")
require("pathlib")
require("fnmatch")
require("html")
require("inspect")
require("io")
require("copy")

# Add one additional module that tmp variant has but standard doesn't
require("datetime")  # Add datetime to test