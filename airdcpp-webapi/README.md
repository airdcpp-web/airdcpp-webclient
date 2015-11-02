# airdcpp-webapi
Websocket/REST JSON API for AirDC++ core

## Dependencies:

* https://github.com/nlohmann/json (included in the repository)
* https://github.com/zaphoyd/websocketpp (unpack the source folder to root when compiling with Visual Studio)
* AirDC++ core

## General information

### Encoding

All messages are encoded in UTF-8.

### Protocols

The API can be accessed through http:// and https://. For Websockets the respective protocols are ws:// and wss://.
You may not switch between encrypted and unencrypted requests during a single session: if the session was authenticated through
an encrypted protocol, all subsequential traffic must be encrypted (and vice versa).

### Path format

The following parameters are common in all requests.

`<api_module>/v<module_version>/<module_section>>`


## Websockets

Websockets use a HTTP-like JSON messaging protocol for communication. You may send a `callback_id` parameter in the request that the server will append to its response.
If no callback id is set, there will be no response.

### Simple requests

Request:

```json
{
"callback_id": 1,
"path": "session/v0/auth",
"method": "POST",
"data": {
	"username": "user1",
	"password": "example_password"
}
}
```

Response:

```json
{
"callback_id":1,
"code":200,
"data": {
  "token":"1623935396",
  "user":"test"
}
}
```

### Subscription

You may subscibe to specific events by sending a POST request with the subscription path. The path uses the following format:

`<api_module>/v<module_version>/listener/<event_name>`

The following example demonstrates how to subscribe for new system log messages.

```json
{
"callback_id": 1,
"path": "log/v0/listener/log_message",
"method": "POST"
}
```

The response for the subscription call will be a HTTP response code without data. In order to remove a subscription, send 
a DELETE request containing the subscription path without data.

Unsubscribing from system log messages:

```json
{
"callback_id": 1,
"path": "log/v0/listener/log_message",
"method": "DELETE"
}
```

Subscription events sent by the socket will contain a `event` parameter specifying the subscription name and `data` parameter containing 
the event-specific data.

Log message event example:

```json
"event": "log_message",
"data": {
    "id": 66,
    "severity": 0,
    "text": "File list refresh finished",
    "time": 1441964384
}
}
```

## Authenticating

**Request**

```
POST /session/v0/auth
```

**Data**

```json
{
	"username": "user1",
	"password": "example_password"
}
```

### Response

**Success**

```
Status: 200 OK
```

```json
{
  "token":"1623935396",
  "user":"test"
}
```

**Error**

```
Status: 401 Unauthorized
Invalid username or password
```


### Associating Websocket to an existing session:

You may associate a Websocket to an existing session by sending the session token.

```json
{
"callback_id": 1,
"path": "session/v0/socket",
"method": "POST",
"data": {
	"authorization": "84353272432645"
}
}
```

Response will be code 200 or error 400 if the session wasn't found.
