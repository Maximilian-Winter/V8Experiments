# Build the V8 JavaScript Engine on Windows for MSVC and cmake for embedding it into C++

## Download and Setup depot_tools
1. Download depot_tools:
   - Visit https://chromium.googlesource.com/chromium/tools/depot_tools.git
   - Click on "clone" and copy the HTTPS URL
   - Open a command prompt and run:
     ```
     git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
     ```

2. Add depot_tools to PATH

3. Fetch V8:
   - Open a new command prompt (to ensure the updated PATH is used)
   - Navigate to the directory where you want to download V8
   - Run the following command:
     ```
     fetch v8
     ```
   - This will take some time as it downloads the V8 source code and its dependencies

## Set environment variables for building
```
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set vs2022_install=C:\Program Files\Microsoft Visual Studio\2022\Professional
```

## Run gn tool for generating build files
```
gn gen out/x64.release --args="is_debug=false target_cpu=\"x64\" v8_static_library=true"
```

## Edit arguments for building
```
gn args out/x64.release
```

### New File Content
```
is_debug = false
target_cpu = "x64"
treat_warnings_as_errors = false
is_component_build = false
symbol_level = 0
v8_use_external_startup_data = false
v8_static_library = true
v8_enable_i18n_support = false
v8_monolithic = true
use_custom_libcxx = false
is_clang = false
```

## Build the V8 libraries
```
ninja -C out/x64.release -j 22
```

Note: The `-j 22` flag specifies the number of parallel jobs for the build process. Adjust this number based on your system's capabilities.





