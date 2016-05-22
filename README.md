countminsketch: an approximate items counter Redis module
===

This is trivial implementation of the Count Min Sketch (CMS) from Graham Cormode and S. Muthukrishnan as described in (Approximating Data with the Count-Min Data Structure)[http://dimacs.rutgers.edu/~graham/pubs/papers/cmsoft.pdf].

CMS is a probabilistic data structure that can be used to keep values associated with items, such as the counts of observations of samples in a stream (e.g. IP addresses, URLs, etc...). The counts are only approximated, i.e. there is a probability for error, but the data structure requires considerably less resources compared to storing all samples and their counts.

This implementation employs Redis' Strings for storing the data structure using direct memory access and 16-bit registers.

Quick start guide
---

1. Build a Redis server with support for modules.
2. Build the countminsketch module: `make`
3. To load a module, Start Redis with the `--loadmodule /path/to/module.so` option, add it as a directive to the configuration file or send a ` MODULE LOAD` command.

CMS API
---

### `CMS.INCRBY key item value [item value ...]`

> Time complexity: O(N), where N is the number of items.

Increments the count for `item` by `value`.

If `key` does not exist, the sketch is initialized using a default width of 2000 and a depth of 10 (the defaults provide a %0.01 error at a probability of %0.01).

**Reply:** Simple String, "OK".

### `CMS.QUERY key item [item ...]`

> Time complexity: O(N), where N is the number of items.
 
Returns the estimated count for `item`.
 
**Reply:** Array of Integers, nil if key or element not found

### `CMS.INITBYDIM key width depth`

> Time complexity: O(1)

Initializes a CMS to the dimensions specified by `width` and `depth`.

**Reply:** Simple String, "OK".

### `CMS.INITBYERR key error probabilty`

> Time complexity: O(1)

Initializes a CMS with a maximum `error` at a given `probability`.

`error` and `probability` should be given as doubles. For example, to specify an error of at most %0.01 at probability of %0.01, use `CMS.INITBYERR key 0.001 0.001`.

**Reply:** Simple String, "OK".

### `CMS.DEBUG key`

> Time complexity: O(1)

Debugging helper - shows the sketch's properties.

**Reply:** Bulk Array.

Contributing
---

Issue reports, pull and feature requests are welcome.

License
---

AGPLv3 - see [LICENSE](LICENSE)

Uses Redis Copyright (C) 2009-2012, Salvatore Sanfilippo, BSD 3-Clause License.
Uses xxHash Copyright (C) 2012-2014, Yann Collet, BSD 2-Clause License.
