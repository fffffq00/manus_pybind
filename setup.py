import os
import sys
import shutil
from setuptools import setup, Extension, find_packages

# Import pybind11 to get include path
import pybind11

# Define paths
sdk_lib_dir = "ManusSDK/lib"
pkg_lib_dir = "manus_pybind/lib"
libs_to_copy = ["libManusSDK.so", "libManusSDK_Integrated.so"]

# Automatically copy libraries from ManusSDK/lib to manus_pybind/lib
if os.path.exists(sdk_lib_dir):
    os.makedirs(pkg_lib_dir, exist_ok=True)
    for lib in libs_to_copy:
        src = os.path.join(sdk_lib_dir, lib)
        dst = os.path.join(pkg_lib_dir, lib)
        if os.path.exists(src):
            print(f"Auto-copying {src} -> {dst}")
            shutil.copy2(src, dst)
else:
    # If ManusSDK/lib doesn't exist, check if libraries are already in manus_pybind/lib
    missing = [lib for lib in libs_to_copy if not os.path.exists(os.path.join(pkg_lib_dir, lib))]
    if missing:
        print(f"Warning: ManusSDK directory not found and package libraries {missing} are missing.", file=sys.stderr)
        print("Please place the ManusSDK folder in the project root.", file=sys.stderr)

# Define C++ extension module
ext_modules = [
    Extension(
        "manus_pybind._manus_pybind",
        sources=["src/manus_client.cpp", "src/pybind.cpp"],
        include_dirs=[pybind11.get_include(), "ManusSDK/include"],
        library_dirs=["manus_pybind/lib"],
        libraries=["ManusSDK"],
        extra_compile_args=["-std=c++17", "-O3", "-fPIC"],
        # $ORIGIN points to the directory containing _manus_pybind.so at runtime,
        # telling the loader to search for libManusSDK.so in the relative lib/ directory.
        extra_link_args=["-Wl,-rpath,$ORIGIN/lib", "-lpthread"],
    )
]

setup(
    name="manus_pybind",
    version="1.1.0",
    description="Python bindings for the Manus SDK Client data-retrieval API",
    author="fzhao",
    packages=find_packages(),
    ext_modules=ext_modules,
    package_data={
        "manus_pybind": ["lib/*.so", "*.pyi"],
    },
    include_package_data=True,
    zip_safe=False,
    python_requires=">=3.7",
)
