\newpage

# Code Coverage

To generate code coverage reports for pgexporter, you must:

- Build with **GCC** and **Debug** mode (`-DCMAKE_BUILD_TYPE=Debug`)
- Have both **gcov** (provided by GCC) and **gcovr** installed

### Installing gcovr

You can install `gcovr` using your operating system's package manager or with `pip` if it is not available as a package.

**On Fedora:**
```sh
dnf install gcc gcovr
```

**On Ubuntu/Debian:**
```sh
sudo apt install gcc gcovr
```

**If gcovr is not available via your package manager, use pip:**
```sh
pip install gcovr
```

Make sure `gcovr` is available in your `PATH` after installation.

### Automatic Coverage Instrumentation

When you build pgexporter with GCC and Debug mode, code coverage instrumentation is automatically enabled if both `gcov` and `gcovr` are found.  
You will see a message like this in your CMake output:
```
Coverage tools (gcov and gcovr) found, compiler is GCC, and build type is Debug: Code coverage ENABLED
```

## Generating Code Coverage Reports

After building pgexporter with GCC and Debug mode (see the Developer Guide), you can generate code coverage reports as follows.

> **All commands below should be run from your build directory.**

1. **Run the test suite**  
   From your build directory, run the test suite:
   ```sh
   ./testsuite.sh
   ```
   Or, you can run your own tests if you prefer.

2. **Generate coverage reports**  
   Still in your build directory, run:
   ```sh
   mkdir -p coverage
   gcovr -r ../src --object-directory . --html --html-details -o coverage/index.html
   gcovr -r ../src --object-directory . > coverage/summary.txt
   gcovr -r ../src --object-directory . --xml -o coverage/coverage.xml
   ```

   - `coverage/index.html`: Detailed HTML report
   - `coverage/summary.txt`: Text summary
   - `coverage/coverage.xml`: XML report (for CI or external tools)

3. **View the results**  
   Open `coverage/index.html` in your browser to see the detailed coverage report.

**Note:**  
Coverage is only available when building with GCC and Debug mode, and when `gcov` and `gcovr` are installed.  
If `gcovr` is not available via your package manager, you can install it using pip:
```sh
pip install gcovr
```
