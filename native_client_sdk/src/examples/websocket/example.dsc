{
  'TOOLS': ['newlib', 'glibc', 'pnacl', 'win'],
  'TARGETS': [
    {
      'NAME' : 'websocket',
      'TYPE' : 'main',
      'SOURCES' : ['websocket.cc'],
      'LIBS': ['ppapi_cpp', 'ppapi']
    }
  ],
  'DEST': 'examples',
  'NAME': 'websocket',
  'TITLE': 'Websocket',
  'DESC': """
The Websocket example demonstrates how to use the Websocket API.  The
user first connects to the test server: ws://html5rocks.websocket.org/echo by
clicking on the 'Connect'' button.  Then hitting Send' will cause the app to
send the message to the server and retrieve the reply.""",
  'INFO': 'Websockets'
}

