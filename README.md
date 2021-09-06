[![GitHub Actions][build-badge]][build]

# AirDC++ Web Client

AirDC++ Web Client is a locally installed application, which is designed for frequent sharing of files or directories within groups of people in a local network or over internet. The daemon application can be installed on different types of systems, such as on file servers and NAS devices.

The application uses [Advanced Direct Connect](https://en.wikipedia.org/wiki/Advanced_Direct_Connect) protocol, which allows creating file sharing communities with thousands of users. The application itself is highly optimized even for extreme use cases: a single client can be used to share more than 10 million files or hundreds of terabytes of data.

### Key functionality

- Responsive [web user interface](https://github.com/airdcpp-web/airdcpp-webui) written in [React.js](https://facebook.github.io/react/)
- Share selected local directories with other users
- Search for files shared by other users
- Save files on disk or view them via the browser
- Chatting capabilities (group and private chat)
- Browse directories shared by other users with a simple file browser interface
- [Extension support](https://github.com/airdcpp-web/airdcpp-extensions) using [Node.js](https://nodejs.org/en/) (+others)
- [Web API (HTTP REST and WebSockets)](http://apidocs.airdcpp.net)

## Download

Download information is available on the [home page](https://airdcpp-web.github.io)

## [Try the online demo](http://webdemo.airdcpp.net)

AirDC++ Web Client wraps the following subprojects:

* [AirDC++ Core](https://github.com/airdcpp/airdcpp-core)
* [AirDC++ Web API](https://github.com/airdcpp/airdcpp-webapi)
* [AirDC++ Web UI](https://github.com/airdcpp-web/airdcpp-webui)

## Resources

* [Home page](https://airdcpp-web.github.io)
* [Installation guide](https://airdcpp-web.github.io/docs/installation/installation.html)
* [Contributing information](https://github.com/airdcpp-web/airdcpp-webclient/blob/master/.github/CONTRIBUTING.md)
* [API reference](http://apidocs.airdcpp.net)

## Feature requests or questions?

The issue tracker can be used for feature requests and questions as well. You may also upvote existing feature requests to increase  their likelihood for being implemented.

Development/support hub is at adcs://web-dev.airdcpp.net:1511
