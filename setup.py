#!/usr/bin/env python3
import os
import sys
import platform
import subprocess
from setuptools import setup, Extension, find_packages
from setuptools.command.build_ext import build_ext


class CMakeExtension(Extension):
    """Custom extension for CMake-based builds"""
    def __init__(self, name):
        # Don't add any source files, CMake handles that
        Extension.__init__(self, name, sources=[])
        # Store the package directory as a property
        self.sourcedir = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))


class CMakeBuild(build_ext):
    """Custom build command for CMake-based extensions"""
    def build_extension(self, ext):
        # Required: set the output directory for the extension
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep
            
        # Configure CMake build options
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}",
            f"-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_CXX_STANDARD=17",
        ]
        
        # Set build arguments based on platform
        build_args = []
        if platform.system() == "Windows":
            cmake_args += ['-A', 'x64'] if sys.maxsize > 2**32 else []
        else:
            build_args += [f"-j{os.cpu_count()}"]
            
        # Create the build directory
        build_temp = os.path.join(self.build_temp, ext.name)
        if not os.path.exists(build_temp):
            os.makedirs(build_temp)
            
        print(f"Building extension {ext.name} in {build_temp}")
        print(f"CMake args: {cmake_args}")
        print(f"Build args: {build_args}")
        print(f"Source directory: {ext.sourcedir}")
        
        # Run CMake to configure and build the extension
        try:
            subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=build_temp)
            subprocess.check_call(["cmake", "--build", ".", "--target", "faasm_client_cpp"] + build_args, cwd=build_temp)
            print(f"Successfully built {ext.name}")
        except subprocess.CalledProcessError as e:
            print(f"Build failed: {e}")
            raise


# Main setup function
setup(
    ext_modules=[CMakeExtension("faasmctl.faasm_client_cpp")],
    cmdclass={"build_ext": CMakeBuild},
    # Note: All other package metadata comes from pyproject.toml
)