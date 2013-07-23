verben
======
                  __             
 _   _____  _____/ /_  ___  ____ 
| | / / _ \/ ___/ __ \/ _ \/ __ \
| |/ /  __/ /  / /_/ /  __/ / / /
|___/\___/_/  /_.___/\___/_/ /_/ 
                                 
![Avatar](https://github.com/flygoast/verben/misc/verben.png)


Introduction
------------
`verben` is a TCP server framework that you can customize your logical 
business code. `verben` is responsible for networking processing. 
You need not to care it.

In `plugins` directories, there are some plugin demos. You can refer
them to write your business code.

Building & Running
------------------
1) clone verben from github

    git clone https://github.com/flygoast/verben

2) build verben core

    cd verben/src
    make && make install

3) build plugin, e.g. echo

    cd ../plugin/echo
    make && make install

4) Running verben

    cd ../../bin/
    verben

Plugins
-------
* **echo**: Sample verben plugin implementing a echo server.
* **http**: Simple HTTP server.
* **vlua**: Make you be able to process your business with `Lua`.

Author
------
FengGu <flygoast@126.com>
