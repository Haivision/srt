Utilities in the SRT library
============================

1. Endian utilities
-------------------

* HtoNLA, NtoHLA, HtoILA, ItoHLA : endian operations on arrays

Markers:
   * H = hardware (endian of the current hanrdware)
   * N = network (big endian)
   * I = Intel (little endian)

Following letters L and A mean "long" (32-bit) and "array". The order of
arguments follows the declaration of `memcpy`.

2. Static bit numbering utility
-------------------------------

This is something that allows you to turn 32-bit integers into bit fields.
Although bitfields are part of C++ language, they are not designed to be
interchanged with 32-bit numbers, and any attempt to doing it, like by placing
inside a union, for example, is nonportable (order of bitfields inside
same-covering 32-bit integer number is dependent on the endian), so they are
popularly disregarded as useless. Instead the 32-bit numbers with bits
individually selected is preferred, with usually manual playing around with &
and | operators, as well as << and >>. This tool is designed to simplify the
use of them. This can be used to qualify a range of bits inside a 32-bit number
to be a separate number, you can "wrap" it by placing the integer value in the
range of these bits, as well as "unwrap" (extract) it from the given place. For
your own safety, use one prefix to all constants that concern bit ranges
intended to be inside the same "bit container".

Usage: `typedef Bits<leftmost, rightmost> MASKTYPE;  // MASKTYPE is a name of your choice.`

Note that rightmost defaults to leftmost, so you can also mark a single bit only.
REMEMBER: leftmost > rightmost because bit 0 is the LEAST significant one.

With this defined, you can use the following members:

Static constants:

- MASKTYPE::mask - to get the `int32_t` value with bimask (used bits set to 1, others to 0)
- MASKTYPE::offset - to get the lowermost bit number, or number of bits to shift
- MASKTYPE::size - number of bits in the range

Static methods:

- `bool MASKTYPE::fit(uint32_t value)`
   - check if the value is small enough to be encoded with this range of bits.
     For example: if our bitset mask is 00111100, this checks if given value fits in
     00001111 mask (that is, does not exceed <0, 15> range).

- `uint32_t MASKTYPE::wrap(uint32_t value)`
    - gets the value that should be placed in appropriate bit range and
      returns a whole 32-bit word that has the value already at specified place.
      To create a 32-bit container that contains already all values destined for different
      bit ranges, simply use wrap() for each of them and bind them with | operator.

- MASKTYPE::unwrap(int bitset)
   - extracts appropriate bit range and returns them as normal integer value

Example:
```
	typedef Bits<7, 4> Cipher1;
	typedef Bits<3, 0> Cipher2;

	uint8_t value = GetByteValue();
	
	uint32_t c[2] = {
		Cipher1::unwrap(value),
		Cipher2::unwrap(value)
	};

	for (int i = 0; i < 2; ++i)
		cout << (c[i] < 10 ? c[i] + '0' : c[i] - 10 + 'A');
	cout << endl;
```

3. DynamicStruct: a simple array that can be only indexed with a dedicated type.
--------------------------------------------------------------------------------

* `class DynamicStruct<class FieldType, size_t ArraySize, class IndexerType>`

This is something that reminds a structure consisting of fields of the same
type, implemented as an array. It's parametrized by the type of fields and the
type, which's values should be used for indexing (preferably an enum type).
Whatever type is used for indexing, it is converted to `size_t` for indexing
the actual array.

The user should use it as an array: `ds[DS_NAME]`, stating that `DS_NAME` is of
enum type passed as 3rd parameter. However trying to do ds[0] would cause a
compile error.


4. FixedArray: a simple wrapper for a dynamically allocated array.
------------------------------------------------------------------

Provides a wrapper of all basic operations, `operator[]` as well as basic
container methods: `begin(), end(), data(), size()` to satisfy the concept
of the STL random-access container.


5. HeapSet: a partially sorted container using the heap tree concept
--------------------------------------------------------------------

Declaration:
```
template <class NodeType, class Access = NodeType>
class HeapSet
```

This container implements a concept of a partially sorted container which
guarantees always the element at the head to be the earliest in the sorting
order, and allows elements to be added to the container with partial sotring.
The element is added at the quickest findable position in the tree, while
pulling the earliest element causes tree rebalancing.

The types for the template instantiation are:

- NodeType: The type of the value kept in the container (representation of
the contained objects). This type must be a lightweight-value type, so prefer
things like integers, pointers or iterators. There must also exist a trap
representation for this type.

- Access: a class that provides static methods according to the requirements

The elements kept in this container must provide a node functionality (data
strictly related to this container) and two most important elements of the
node:

- KEY: This value decides about the sording order of the elements.
- POSITION: This value of `size_t` type defines the current position of the
  element in the heap array

The POSITION is the cache of the index in the internal heap array; it is
being used for moving the element throughout the container if needed, and
so it is also being updated accordingly. For that reason you should never
modify it yourself and always initialize it with the trap representation
value (designaed as `std::string::npos`, also replicated as `npos` constant
inside the HeapSet container), which means that the element is not in the
heap array.

The NodeType should be a value through which the object in the container is
directly reachable, so for example:
- A pointer to the object - NULL is a trap representation
- A positive integer index in some array - so std::string::npos is a trap
- A list iterator - for that you need to keep some empty list for a trap
- Your own wrapper for any of the above so that it can be same as AccessType

The AccessType class is only required to contain several static members, which
will be operating on either `NodeType` or `key_type`. The following things must
be provided by the AccessType:

- `typedef key_type`: the type of the key value field
- `static key_type& key(NodeType)` : provide reference to the key field
- `static size_t& position(NodeType)` : provide reference to the position field
- `static NodeType none()` : return trap representation for NodeType
- `static bool order(key_type left, key_type right)` : true if left < right

You can just as well keep the same object in multiple HeapSet containers,
you just need to have separate node entries for each one (sharing the same
NodeType is possible, just use different AccessType).

HeapSet state attributes:

- `none()` : returns the trap representation for NodeType (as provided by
            the AccessType class), for convemience
- `npos` : an internal static constant assigned from std::string::npos
- `raw()` : returns the constant reference to the internal heap array
- `empty(), size()` : same as for the internal array
- `operator[]` : return node at given position (UNCHECKED!)

Operations:

- `find_next(key_type k)`: return the node that is the earliest element in the
                         list, but already later than the given `k` key
                         (none() if no such element)
- `top()` : return the element at top. Returns `none()` if the heap is empty.
- `top_raw()` : Unchecked version of `top()`, returns the value from the first
                element of the internal array; results in UB if it's empty.
- pop() : same as top(), but the element is removed from the list.
- insert() : insert the element into the heap array. The element's position
             must be npos first. It's in two versions:
           - insert(node): insert the node after you updated the key
           - insert(key, node): convenience wrapper for updating and inserting
- erase() : removes the element from the heap array. Returns false if the
            element isn't in the array. Updates the position to `npos`.
- update(pos, newkey): update the node at the given position with the new
                       key and update its position accordingly


6. `explicit_t`: Prevent C++ from using default conversion
----------------------------------------------------------

Using `explicit_t<int>` instead of `int` as a function argument prevents
the function from being called with any other type, like `bool` or `long`.


7. `EqualAny`: shorthand comparison of a single value to multiple values
------------------------------------------------------------------------

Usage example:
```
if (EqualAny(state), ST_CONNECTING, ST_CONNECTED, ST_BROKEN)
  ...
```

It's a shortened version of:
```
if (state == ST_CONNECTING || state == ST_CONNECTED || state == ST_BROKEN)
   ...
```

You need to add `using namespace any_op` inside the function to enable it.


8. Unique pointer and movable objects
-------------------------------------

For C++11 these are aliases: `UniquePtr = std::unique_ptr` and `Move = std::move`.

For C++03 they are provided with specific definitions resembling partiallty
this functionality.


9. Map element extraction convenience functionalities
-----------------------------------------------------

These functions do a similar thing as `m[k]` for an `m` map with given `k` key.
Unlike `operator[]` they do not reinsert a key into the map if it doesn't exist,
instead they return a specific value in this case. They wrap `map::find` call,
but the result is translated to a more convenient value:

* `map_get(m, k, def = default)`: if not found, returns the given `def` value,
   which defaults to the defauilt-constructed mapped type.

* `map_getp(m, k)`: returns a pointer to the mapped type value, if the key is
   present in the map, otherwise returns NULL (nullptr, if C++11).

* `map_tryinsert`: ensures that the array contains a key and returns the reference.

This function replaces partially the functionality of std::map::insert.
Differences:

- inserts only a default value
- returns the reference to the value in the map
- works for value types that are not copyable

The reference is returned because to return the node you would have
to search for it after using operator[].

NOTE: In C++17 it's possible to simply use `map::try_emplace` with only
the key argument and this would do the same thing, while returning a
pair with iterator.

* `MapProxy`: A proxy object with a reference to map and the key

Using this type you can create a map-key assignment without modifying
the map. Having that you can do:

   - p.find() - same as map.find(k)
   - val = p; - extract the value by assigning to the value type, or default if not found
   - p.deflt(defval) - same as above, but return `defval` if not found
   - p.exists() - returns true if the key is presnet in the map
   - p.dig() - uses `map_tryinsert` and returns its result


10. Printable, PrintabeMod: allow formatting a container of values
-------------------------------------------------------------------

These functions turn a container of printable values into the representing
string with surrounding `[]` and values separted by space. Used in logging.


11. Container utilities and algorithms
--------------------------------------

* `FilterIf`: a mix of `std::find_if` and `std::transform`: copies the range
   defined by first two iterators into the target designated by the output
   iterator. The function must have `result_type` type declared inside and
   the call to its `operator()` must return a pair of this type and `bool`.
   The value of result's `first` is written to the output if in this call the
   `second` boolean value is true.

* `insert_uniq`: a poor-performance insertion to the vector, with first check
   if the value is already there, in which case nothing is inserted.

* `Tie`: similar to `std::tie` for C++03: binds two variables by exposing
   they references so that this can be used in the assignment

* `All`: returns a pair of iterators extracted from `begin()` and `end()`

* `Size`: a version of std::size from C++11 - for a fixed array it returns
   the number of declared elements; for other types it's size() method result.

* `safe_advance` : same as `std::advance`, but additionally you specify
   the iterator beyond which the advancement shall not be done; returned
   is the value by which the iterator was really advanced. Only the forward
   iterator concept is supported, though; for random-access containers
   you should do it manually with checking size() and distance()

* `FringeValues`: Takes all values from the container and marks in the
   output map, how many values of that kind were found. The output map
   will then contain only unique values as keys and the value is the
   number of found occurrences of this very value


12. CallbackHolder
------------------

A convenience wrapper for a function pointer with opaque pointer idiom.
An additional macro `CALLBACK_CALL` simplifies passing parameters to
the call regarding the opaque pointer.


13. Pass filter utilities
--------------------------

* GetPeakRange

This utility is used in window.cpp where it is required to calculate the median
value basing on the value in the very middle and filtered out values exceeding
its range of 1/8 and 8 times. Returned is a structure that shows the median and
also the lower and upper value used for filtering.

This calculation does more-less the following:

1. Having example window:
  - 50, 51, 100, 55, 80, 1000, 600, 1500, 1200, 10, 90

2. This window is now sorted, but we only know the value in the middle:
  - 10, 50, 51, 55, 80, [[90]], 100, 600, 1000, 1200, 1500

3. Now calculate:
  - lower: `90/8 = 11.25`
  - upper: `90*8 = 720`

4. Now drop those from outside the `<lower, upper>` range:
  - `10, (11<) [ 50, 51, 55, 80, 90, 100, 600, ] (>720) 1000, 1200, 1500`
 
5. Calculate the median from the extracted range.
   NOTE: the median is actually repeated once, so size is +1.

   values = { 50, 51, 55, 80, 90, 100, 600 };
   sum = 90 + accumulate(values); ==> 1026
   median = sum/(1 + values.size()); ==> 147

For comparison: the overall arithmetic median from this window == 430

* AccumulatePassFilter

This function sums up all values in the array (from p to end), except those
that don't fit in the low- and high-pass filter. Returned is the sum and the
number of elements taken into account, through a pair.

* AccumulatePassFilterParallel

This function sums up all values in the array (from p to end) and
simultaneously elements from `para`, stated it points to an array of the same
size. The first array is used as a driver for which elements to include and
which to skip, and this is done for both arrays at particular index position.
Returner is the sum of the elements passed from the first array and from the
`para` array, as well as the number of included elements.


14. DriftTracer
---------------

This is the utility for calculating the drift in SRT, which is measured as the
smoothed average time distance between the expected and actual arrival time of
a packet.

You should update it with every time read value, so the value is added. Only
up to maximum history is kept in the container. A special value is declared as
"overdrift", which means that the clock skew seems to be serious and should be
taken as a legitimate difference to fix.

The values of `drift()` and `overdrift()` can be read at any time, however if
you want to depend on the fact that they have been changed lately, you have to
check the return value from update().

IMPORTANT: drift() can be called at any time, just remember that this value may
look different than before only if the last update() returned true, which need
not be important for you.

* CASE: `CLEAR_ON_UPDATE = true`

overdrift() should be read only immediately after update() returned true. It
will stay available with this value until the next time when update() returns
true, in which case the value will be cleared. Therefore, after calling
update() if it retuns true, you should read overdrift() immediately an make
some use of it. Next valid overdrift will be then relative to every previous
overdrift.

* CASE: `CLEAR_ON_UPDATE = false`

overdrift() will start from 0, but it will always keep track on any changes in
overdrift. By manipulating the `MAX_DRIFT` parameter you can decide how high the
drift can go relatively to stay below overdrift.

15. Running Average Utilities
-----------------------------

* CountIIR(base, newval, factor)

Returns the new value of the running average, while having the current base
value specified by `base` - if 0, the new value is taken as the new average
value. The new average value is the current base modified by the new value
taken by factor (use 0 - 1 range for this value).

* `avg_iir<DLEN>(base, newval)`

This uses base as if it was an average value of the previous `DLEN` values
and adds `newval` to calculate the new average value.

16. Property accessor definitions
---------------------------------

This is a system of turning an existing field into being accessible in specific
mode through extra methods.

"Property" is a special method that accesses given field. This relies only on
a convention, which is the following:

```
V x = object.prop(); <-- get the property's value
object.set_prop(x); <-- set the property a value
```

Properties might be also chained when setting:

```
object.set_prop1(v1).set_prop2(v2).set_prop3(v3);
```

Properties may be defined various even very complicated ways, which is simply
providing a method with body. In order to define a property simplest possible
way, that is, refer directly to the field that keeps it, here are the following
macros:

Prefix: `SRTU_PROPERTY_`

Followed by:

 - access type: RO, WO, RW, RR, RRW
 - chain flag: optional `_CHAIN`, for WO, RW and RRW only

Where access type is:

- RO - read only. Defines reader accessor. The accessor method will be const.
- RR - read reference. The accessor isn't const to allow reference passthrough.
- WO - write only. Defines writer accessor.
- RW - combines RO and WO.
- RRW - combines RR and WO.

The `_CHAIN` marker is optional for macros providing writable accessors
for properties. The difference is that while simple write accessors return
void, the chaining accessors return the reference to the object for which
the write accessor was called so that you can call the next accessor (or
any other method as well) for the result.






