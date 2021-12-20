# Introduction
Restgoose is a C++ libary built on top of the Cesanta Mongoose webserver library. It contains the following 
- A RESTful/Websocket Server
- An HTTP client
- A Websocket client

The library should run on Linux and Windows.

# Dependencies
The following libraries are required from GitHub. Using the CMake build will automatically clone them to your home directory.
- [log](https://github.com/martim01/log) - a simple logging library
- [JsonCPP](https://github.com/open-source-parsers/jsoncpp) - a JSON library for C++
- [mongoose](https://github.com/cesanta/mongoose) - the embedded web server from Cesanta

In order to allow secure connections you also need openssl. 
On a Debian Linux build
```
sudo apt install libssl-dev
```

# Building
There is a CodeBlocks project file to build the library.
Alternatively there is a CMakeLists.txt file which can be used to build the library on Linux (Windows etc still to come). To use this:
```
cd build
cmake ..
cmake --build .
```

# Usage

Build the library as above and link to the compiled library.

## Server

- [Creation](#Creation)
- [Setup](#Setup)
- [Callback](#Callbacks)
- [Run](#Run)
- [Websockets](#Websockets)
- [Basic Authentication](#BasicAuthentication)
- [Thread Mode](#ThreadMode)

### Creation
Create a Server instance
```
auto server = pml::restgoose::Server();
```

### Setup
Before running the server you need to Initialize it by calling the `Init` function with the following arguments
- sCert - the path to a TLS certificate file. Send an empty string if you are not using HTTPS
- sKey  - the path to a TL key file. Send an empty string if you are not using HTTPS
- nPort - the port number that you want the Server to listen on
- apiRoot - the relative URL that is the "root" node for all the RESTful endpoints. 
- bEnableWebsocket - send true if you want the Server to act as a websocket server as well as an HTTP server

```
server.Init(pathToCertFile, pathToKeyFile, tcpPort, "/api", false);
```

If you want the Server to serve static HTTP web pages as well as be a RESTful interface then you need to tell the server the location of the pages
```
server.SetStaticDirectory(pathToWebPages);
```


### Callbacks
You need to provide a callback function for each HTTP method and "endpoint" that you want the Server to handle.
You can also add a callback function to handle any request that does not "hit" and endpoint and a callback to be called every time the server loops.

#### RESTful Endpoints
 Add the RESTful endpoints that you want the server to handle passing 
- the HTTP method to handle
- the relative url of the endpoint
- a callback function that handles the endpoint

```
server.AddEndpoint(pml::restgoose::GET, endpoint("/api"), rootCallbackfunction) 
server.AddEndpoint(pml::restgoose::GET, endpoint("/api/versions"), versionCallbackfunction)
```
##### The Callback Function
The callback function has the following format
```
pml::restgoose::response callback(const query& theQuery, const std::vector<partData>& data, const endpoint& theEndpoint, const userName& user)
```
###### theQuery
A string containing any query data in the url (the part of the URL after a ?)
###### vData 
A vector containing any data sent to the server (via a POST, PATCH or PUT request). 
The data is wrapped in a pml::restgoose::partData structure which has the following variables
```
std::string sHeader;  // in a multipart upload contains the part specific "header" (e.g. Content-Disposition)
partName name;        // in a multipart/form upload contains the name of the form object.
textData data;        // contains any non-binary data that has been sent (or in a multipart/form upload contains the filename if a file is being sent)
fileLocation filepath;// if a file has been uploaded to the server contains the temporary location that the file has been stored at.
```
###### theEndpoint
A string containing the relative URL of this endpoint.
###### user
If Basic Authentication is being used contains the name of the user passed in the Basic Authentication header
###### Return: pml::restgoose::response
The callback function should return a pml::restgoose::response object to instruct the Server what to send back to the client. This has the following data members
```
unsigned short nHttpCode;     // the HTTP response code (e.g. 200, 404 etc)
Json::Value jsonData;         // any data to send back to the client in JSON format if the content-type is "application/json"
headerValue sContentType;     // a string containing the content-type of the response (e.g. "text/plain"). This defaults to "application/json"
textData data;                // any data to send back if the content-type is not "application/json"
```
#### NotFound Callback
This callback function will be called whenever a client attempts to connect to an endpoint within the "api tree" that has no EndpointCallback assigned.
It allows the user to send a bespoke "not-found" message back to the client.
The format is the same as the Endpoint callback above. 
If no callback is set then the Server will return a 404 response.

#### Loop Callback
The server runs a "polling" function with a timeout value. If a LoopCallback function is set then it will be called each time the polling function is complete. 
The function is passed the number of milliseconds since the last time it was called.

### Run
To start the Server call the `Run` method. You can run the server in its own thread by passing __true__ as the first argument and choose the poll timeout time 
 ```
 server.Run(true, std::chrono::milliseconds(1000));
 ```

Top stop the server call the `Stop` method.

### Websockets
The Server supports Websocket connections. It currently expects any data sent to the Server to be valid JSON.

To enable Websocket support pass __true__ as the last argument of the `Init` function.

Then call `AddWebsocketEndpoint` to add endpoints that websocket clients can connect to and callback functions.
There are 3 callback functions
- funcAuthentication - called when a websocket client attempts to authenticate itself
- funcMessage - called when a websocket client sends the server a message
- funcClose - called when a websocket client disconnects.

#### funcAuthentication
If you are using basic authentication as a security measure on the server then the first message a websocket client must send to the server after connection must be an authentication request.
This must have the following format
```
{
  "action" : "_authentication",
  "user" : theUserName,
  "password" : thePassword
}
```
The server will check whether the user/password pair is correct and if so will then call the __funcAuthentication__ callback passing 
- the relative url the client websocket connected to
- the username
- the ip address of the client.
The callback should return __true__ to allow the connection to take place and __false__ to terminate it.

#### funcMessage
Every time (an authenticated) client websocket sends the Server a message the __funcMessage__ callback is run. It is passed
- the relative url the client websocket connected to
- the sent data as a Json::Value object
The callback should return __true__ to allow the connection to continue and __false__ to terminate it.

#### funcClose
Should a client websocket terminate the connection then the __funcClose__ callback is run. It is passed
- the relative url the client websocket connected to
- the ip address of the client.


### Sending Websocket Messages To A Client
To send a websocket message from the server use the `SendWebsocketMessage` function. This has the following arguments
- set<endpoint> - the message will be sent to any client that has connected to an endpoint in this set, or an endpoint directly relative to one in this set. _So a message sent to __/api/ws1__ would be sent to a client connected to __/api/ws1__ and also one connected to __/api/ws1/sub__ but not one connected to __/api/ws2__._
- Json::Value - the message to send in JSON format.

#### Adding extra subscriptions to a client
By default a client websocket will receive messages directed to the endpoint it is connected to. It is also possible for the client websocket to subscribe to additional endpoints.
To do this it must send a JSON message in the following format
```
{
    "action" : "_add",
    "endpoints" : [ array of relative URL endpoints ]
}
```
It can also remove subscription by sending
```
{
    "action" : "_remove",
    "endpoints" : [ array of relative URL endpoints ]
}
```
### BasicAuthentication
The server currently only supports Basic Authentication. It only makes sense to use this if you are also using TLS.
To enable Basic Authentication simply add a user with their password
```
server.AddBAUser(theUserName, thePassword);
```

### ThreadMode
The server can either be run in the same thread as the main program or in a separate thread. You set this in the `Run` function.
If running as a separate thread it may be necessary to freeze the Server thread whilst the main thread gets some data for the Server thread to send back. There are a number of functions to facilitate this.

In the same thread as the Server (e.g. in an Endpoint Callback function). Call `PrimeWait` and `Wait`. This will freeze the Server thread at the point that `Wait` is called.

Once you want the thread to continue call `Signal` passing a pml::restgoose::response object back.

Eg. In an endpoint callback
```
 ...
server.PrimeWait();
//Signal to main thread that we need some data
 ...
//Wait for the main thread to reply
server.Wait();
//Do something with the data
return server.GetSignalData();
```
and in the main thread
```
 ...
//Get some data
response resp;
resp.nHttpCode = 200;
resp.jsonData["result"] = "sucess";
server.Signal(resp);
```






