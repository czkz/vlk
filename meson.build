project('vlk', 'cpp',
  version : '0.1',
  default_options : ['warning_level=3', 'cpp_std=c++20'])

glfw = dependency('glfw3')
vulkan = dependency('vulkan')
threads = dependency('threads')

executable('vlk',
           'main.cpp',
           dependencies : [ glfw, vulkan, threads ],
           include_directories : [ '/home/dek/proj/async2/libs', '/home/dek/proj/cp' ],
           install : true)
