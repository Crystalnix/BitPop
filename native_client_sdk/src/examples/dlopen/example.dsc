{
  'TOOLS': ['glibc'],
  'TARGETS': [
    {
      'NAME': 'dlopen',
      'TYPE': 'main',
      'SOURCES': ['dlopen.cc'],
      'LIBS': ['dl', 'ppapi_cpp', 'ppapi', 'pthread']
    },
    {
      'NAME' : 'libeightball',
      'TYPE' : 'so',
      'SOURCES' : ['eightball.cc', 'eightball.h'],
      'CXXFLAGS': ['$(NACL_CXXFLAGS)', '-fPIC'],
      'LIBS' : ['ppapi_cpp', 'ppapi', 'pthread']
    }
  ],
  'DEST': 'examples',
  'NAME': 'dlopen',
  'TITLE': 'Dynamic Library Open',
  'DESC': """
The dlopen example demonstrates how build dynamic libraries and then
open and use them at runtime.  When the page loads, type in a question and
hit enter or click the ASK! button.  The question and answer will be
displayed in the page under the text entry box.  Shared libraries are only
available with the GLIBC toolchain.""",
  'INFO': 'Teaching focus: Using shared objects'
}

