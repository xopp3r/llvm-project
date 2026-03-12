.. title:: clang-tidy - bugprone-no-bitfield-in-union

bugprone-no-bitfield-in-union
=============================

MISRA rule 6.3

A member of a union shall not be declared as a bit field.

The exact bitwise position of a bit field within a storage unit is implementation 
defined, Therefore, if two bit fields are declared such that they fit within the 
same storage unit of a union, the compiler is not required to overlay them over
one another beginning from the starting bit of the storage unit.

If the union is used for type-punning, it is therefore unclear which bits of the 
previously-stored value will be accessed by the bit field.

If the union is not intended to be used for type-punning, there is no point in 
declaring the members as bit fields, because no space will be saved 
(a complete storage unit will need to be allocated within the union anyway).

This rule does not apply to sub-objects within union members that do not themselves have a union type.
