%CLIObject = type {i8*, i8*, i8*}

;;; Types for CLI arrays. A size of 0 means an undefined size.
%CLIArray = type { %CLIObject, i32 }
%ArrayBoolean = type { %CLIObject, i32, [0 x i8] }
%ArrayUInt8   = type { %CLIObject, i32, [0 x i8] }
%ArraySInt8   = type { %CLIObject, i32, [0 x i8] }
%ArrayChar    = type { %CLIObject, i32, [0 x i16] }
%ArraySInt16  = type { %CLIObject, i32, [0 x i16] }
%ArrayUInt16  = type { %CLIObject, i32, [0 x i16] }
%ArraySInt32  = type { %CLIObject, i32, [0 x i32] }
%ArrayUInt32  = type { %CLIObject, i32, [0 x i32] }
%ArraySInt64  = type { %CLIObject, i32, [0 x i64] }
%ArrayUInt64  = type { %CLIObject, i32, [0 x i64] }
%ArrayIntPtr  = type { %CLIObject, i32, [0 x i32*] }
%ArrayUIntPtr = type { %CLIObject, i32, [0 x i32*] }
%ArrayFloat   = type { %CLIObject, i32, [0 x float] }
%ArrayDouble  = type { %CLIObject, i32, [0 x double] }
%ArrayObject  = type { %CLIObject, i32, [0 x %CLIObject*] }

%CacheNode = type {i8*, i8*, i8*, i8*, i8*, i1}

%Enveloppe = type {i8*, %CacheNode*, i8*, i8*}

declare void @MarkAndTrace(%CLIObject*)
declare void @CLIObjectTracer(%CLIObject*)

declare %CLIObject* @initialiseClass(i8*)

declare %CacheNode* @n3VirtualLookup(%CacheNode*, %CLIObject*)

declare %CLIObject* @newArray(i8*, i32)
declare %CLIObject* @newObject(i8*)
declare %CLIObject* @newMultiArray(i8*)
declare %CLIObject* @newString(i8*)

declare %CLIObject* @initialiseObject(i8*, i32) readnone

declare i32 @arrayLength(%CLIArray*) readnone

declare void @n3NullPointerException()
declare void @n3ClassCastException()
declare void @indexOutOfBounds(%CLIObject*, i32)

declare void @ThrowException(%CLIObject*)
declare void @ClearException()
declare i8* @GetCppException()
declare %CLIObject* @GetCLIException()
declare i1 @CompareException(i8*)

declare i32 @n3InstanceOf(%CLIObject*, i8*)

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;; Debugging methods ;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

declare void @n3PrintExecution(i32, i32)

