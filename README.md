LogDiff
-------

LogDiff helps you find out what went wrong in a ProcMon trace. It does the following:

- takes two related traces from ProcMon (the Windows Internals API monitor)
- splits them into separate threads
- matches the threads from one trace to the other, based on similarity
  (basically identifies which thread is which)
- displays all the threads and their details in a sortable table
- edits out all the numbers, timestamps, pointers, etc that shouldn't matter when comparing diffs
- shows you a visual diff of any pair of threads, so you can see where the differences actually are

It uses KDiff3, GNU diff and grep, and Qt 4 (the Windows binary includes everything required to run). 
Tested on Windows, should compile on Unix.

License
-------

LogDiff is released under the Apache 2.0 License. See the LICENSE file for details.

Downloads
---------

Project page: <https://github.com/patraulea/LogDiff>

Windows Binaries: <https://github.com/patraulea/LogDiff/downloads>

The 5MB binary will extract Qt libs and the diff/grep tools to %APPDATA%\LogDiff on first run.

Screenshots
-----------
![Thread Matches](https://github.com/patraulea/LogDiff/raw/master/images/logdiff-matches.png)

![Thread Diff](https://github.com/patraulea/LogDiff/raw/master/images/logdiff-diff.png)

Contact
-------

reverse(moc.liamg@cojrah)
