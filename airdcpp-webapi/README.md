# AirDC++ Web API

A Websocket/REST API for applications using the [AirDC++ core](https://github.com/airdcpp/airdcpp-core), such as [AirDC++ (Windows)](https://github.com/airdcpp/airgit) and [AirDC++ Web Client](https://github.com/airdcpp-web/airdcpp-webclient/).

Consult the documentation of the respective project for more information about setting up API access.

## Communicating with the API

There are two different protocols that can be used to communicate with the API: HTTP and Websockets.

You may not switch between encrypted and unencrypted requests within a single session: if the session was authenticated through
an encrypted protocol, all subsequential traffic must be encrypted (and vice versa).


### HTTP

You may use a [RESTful HTTP(S)](https://en.wikipedia.org/wiki/Representational_state_transfer) access for fetching and modifying data with the common HTTP methods (GET, POST, PUT, DELETE, PATCH). Adding event listeners isn't supported.

### Websockets

Websockets enable two-way (bi-directional) communication that allows you to receive notifications about various different client events (such as new messages, share changes and session updates). Additionally all functionality provided via HTTP access is supported as well.

The best way to access the API with Websockets is to use a connector library that will abstract away low level protocol communication. 
Websocket API connectors are currently available for the following programming languages:

* [Javascript](https://github.com/airdcpp-web/airdcpp-apisocket-js)

If you prefer using other programming languages, you may write your own connector or use the raw Websocket API for communication. See the [Websocket API documentation](#) for details.

### Path format

The following base path format is common for all API requests.

`<api_module>/v<module_version>/<module_section>`
