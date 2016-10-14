# Posting a feature request?

Unless it's totally obvious, it's always good to explain your user case and why the requested feature is needed. Features with good reasoning are much more likely to get implemented.

# Making a pull request?

All pull requests should be submitted to the `develop` branch.


# Reporting a bug?

If the issue can't be reproduced, try to describe that what happened before the crash/issue occurred.

If the issue can be reproduced, you may also want to try the [development version]( https://airdcpp-web.github.io/docs/installation/compiling.html#installing-a-development-version) to see if it has been fixed already.

### Compiling issues

Attact the full *console* output cmake and make (no cmake log files). Mention the currently used operating system (with version included).

### UI-related issues

If the UI behaves incorrectly, you should open the console of your _web browser_ (`Ctrl+Shift+J` in Chrome and Firefox) and check if there are any errors. It's even better if you manage to reproduce the issue while the console is open, as it will give more specific error messages. Include the errors in your bug report.

Other useful information:

* Browser version and information whether the issue happens with other browser as well
* Screenshots or video about the issue

### Client crash

Include all text from the generated crash log to your bug report. The log is located at ``/home/<username>/.airdc++/exceptioninfo.txt``.

### Client freeze/deadlock

Note that you should first confirm whether the client has frozen and the issue isn't in the UI (try opening the UI in a new tab).

You must have the ``gdb`` package installed before running the following commands.

```
$ cat ~/.airdc++/airdcppd.pid
[number]
gdb
attach [number]
thread apply all bt full
```

*Don't press `q` after the first page of text is shown as there is a lot more to come*. Save the full output to a file and attach it to your bug report.
