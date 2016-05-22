countminsketch: an apporximate frequency counter Redis module
===

This is trivial implementation of the Count Min Sketch (CMS) from Graham Cormode and S. Muthukrishnan as described at (Approximating Data with the Count-Min Data Structure)[http://dimacs.rutgers.edu/~graham/pubs/papers/cmsoft.pdf].

CMS is a probablistic data structure that can be used to keep the count of observations of items in a stream.
This implementation employs Redis' Strings for storing the data structure using direct memory access and 16-bit registers.

Quick start guide
---

1. Build a Redis server with support for modules.
2. Build the countminsketch module: `make`
3. To load a module, Start Redis with the `--loadmodule /path/to/module.so` option, add it as a directive to the configuration file or send a `
MODULE LOAD` command.

CMS API
---

### `CMS.INCRBY key item value [item value ...]`

> Time complexity: O(N), where N is the number of items.

Increments the count for `item` by `value`.

If `key` does not exist, the sketch is initialized using the default width of 2000 and depth of 10 (providing %0.01 error at %0.01 probability).

### `CMS.QUERY key item [item ...]`

> Time complexity: O(N), where N is the number of items.
 
Returns the estimated count for `item`.
 
**Reply:** Array of Integers, nil if key or element not found

### `CMS.INITBYDIM key width depth`

> Time complexity: O(1)

Initializes a CMS with `width` and `depth`.

**Reply:** Simple String, "OK".

### `CMS.INITBYERR key error probabilty`

> Time complexity: O(1)

Initializes a CMS with a maximum `error` at a given `probability`.

`error` and `probability` sould be given as doubles. For example, to specify an error of at most %0.01 at probability of %0.01, use `CMS.INITBYERR key 0.01 0.01`.

**Reply:** Simple String, "OK".

### `CMS.DEBUG key`

> Time complexity: O(1)

Debugging helper - shows the sketch's properties.

Contributing
---

Issue reports, pull and feature requests are welcome.

License
---

AGPLv3 - see [LICENSE](LICENSE)

Uses Redis Copyright (C) 2009-2012, Salvatore Sanfilippo, BSD 3-Clause License.
Uses xxHash Copyright (C) 2012-2014, Yann Collet, BSD 2-Clause License.
