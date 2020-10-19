# rdrview

Command line tool to extract the main content from a webpage, as done by the
"Reader View" feature of most modern browsers. It's intended to be used with
terminal RSS readers, to make the articles more readable on web browsers such
as lynx. The code is closely adapted from
[the Firefox version](https://github.com/mozilla/readability)
and the output is expected to be mostly equivalent.

## Security

This tool is young and written in C, so it's reasonable to wonder about the
potential for memory issues. To be safe, all HTML parsing happens inside a
sandboxed subprocess. Seccomp is used for this purpose.

## Usage

There are three direct dependencies: libxml2, libseccomp and libcurl.
On Debian/Ubuntu, you can install the first two by running (as root):

    apt install libxml2-dev libseccomp-dev

The libcurl package comes in different flavours, depending on the backend that
provides the SSL support. Any of them will do. To install the GnuTLS version:

    apt install libcurl4-gnutls-dev

For _rdrview_ to be useful, you should also get a character mode web browser
such as lynx:

    apt install lynx

The name of the packages might differ in your distribution. On Fedora, for
example, you can install everything with:

    dnf install libcurl-devel libxml2-devel libseccomp-devel lynx

To build _rdrview_, just cd to its directory and run

    make

Now it should be ready to be used. You can try:

    ./rdrview 'https://github.com/eafer/rdrview'
    ./rdrview < local_file.html

For more information, see the man page:

    man ./rdrview.1

If you find _rdrview_ useful and want to install it, become root again and run

    make install

Now you can just call it with `rdrview` and get help with `man rdrview`, like
you would for any other tool in your system.

## Related projects

- https://github.com/NightMachinary/readability-cli: A thin CLI wrapper around Mozilla's _Readability.js_

## Credits

_rdrview_ was written by
[Ernesto A. Fernández](mailto:ernesto.mnd.fernandez@gmail.com),
but it's mainly a transpilation done by hand of Mozilla's _Readability.js_;
which was itself, in their own words, "heavily based on Arc90's
readability.js". This is the original license:

    Copyright (c) 2010 Arc90 Inc

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
