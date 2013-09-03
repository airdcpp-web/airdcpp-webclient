# -*- coding: utf-8 -*-

Import('env')

dcpp_files = env.Glob('*.cpp')

env.Append(CPPDEFINES = 'BUILDING_DCPP')

# Build the dcpp files and create the library
libdcpp  = env.StaticLibrary(target = 'client', source = dcpp_files)

Return('libdcpp')

