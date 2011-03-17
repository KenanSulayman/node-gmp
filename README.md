# node-gmp

## synopsis

node-gmp wraps the libgmp library.  This library has some poor practices
when it comes to erro handling, so much trickery and deception is used to
make this stable under node.  Trickery includes: replacement allocators,
replacement of jumps to abort() in instruction code and finally using
sigsetjmp and siglongjmp to emulate try/throw a across C and signals in a
more stable fashion.

The rest of gmp is just code.

        var gmp = require('gmp')
        var i = gmp.Int("123412341234123412341234123412341234");
        i.div("2").toString() // "61706170617061706170617061706170617"
        var f = gmp.Float("1234123412341234.123412341234", 1024).div("1.337");
        f.toString().replace(/(\.\d{10}).*$/, "$1") // "923054160315059.17981476530867"
        var r = gmp.Rational("22/7");
        r.toString() // "3.142857142857143"

## requirements

   * http://gmplib.org/ -- built with c++ enabled ( ./configure --enable-cxx )


## installation

      # (useful environment variables: CXXFLAGS LINKFLAGS)
      $ node-waf configure build
