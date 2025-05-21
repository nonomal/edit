// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "icu.h"

#include <icu.h>

#include "buffer.h"
#include "os.h"

static int s_icu_ok = -1;
static UText*(U_EXPORT2* fn_utext_close)(UText* ut);
static UText*(U_EXPORT2* fn_utext_setup)(UText* ut, int32_t extraSpace, UErrorCode* status);
static int32_t(U_EXPORT2* fn_u_strlen)(const UChar* s);
static char*(U_EXPORT2* fn_u_strToUTF8WithSub)(char* dest, int32_t destCapacity, int32_t* pDestLength, const UChar* src, int32_t srcLength, UChar32 subchar, int32_t* pNumSubstitutions, UErrorCode* pErrorCode);
static UChar*(U_EXPORT2* fn_u_strFromUTF8WithSub)(UChar* dest, int32_t destCapacity, int32_t* pDestLength, const char* src, int32_t srcLength, UChar32 subchar, int32_t* pNumSubstitutions, UErrorCode* pErrorCode);
static UConverter*(U_EXPORT2* fn_ucnv_open)(const char* converterName, UErrorCode* err);
static void(U_EXPORT2* fn_ucnv_close)(UConverter* converter);
static const char*(U_EXPORT2* fn_ucnv_detectUnicodeSignature)(const char* source, int32_t sourceLength, int32_t* signatureLength, UErrorCode* pErrorCode);
static void(U_EXPORT2* fn_ucnv_convertEx)(UConverter* targetCnv, UConverter* sourceCnv, char** target, const char* targetLimit, const char** source, const char* sourceLimit, UChar* pivotStart, UChar** pivotSource, UChar** pivotTarget, const UChar* pivotLimit, UBool reset, UBool flush, UErrorCode* pErrorCode);

/**
  * Function type declaration for UText.clone().
  *
  *  clone a UText.  Much like opening a UText where the source text is itself
  *  another UText.
  *
  *  A deep clone will copy both the UText data structures and the underlying text.
  *  The original and cloned UText will operate completely independently; modifications
  *  made to the text in one will not effect the other.  Text providers are not
  *  required to support deep clones.  The user of clone() must check the status return
  *  and be prepared to handle failures.
  *
  *  A shallow clone replicates only the UText data structures; it does not make
  *  a copy of the underlying text.  Shallow clones can be used as an efficient way to
  *  have multiple iterators active in a single text string that is not being
  *  modified.
  *
  *  A shallow clone operation must not fail except for truly exceptional conditions such
  *  as memory allocation failures.
  *
  *  A UText and its clone may be safely concurrently accessed by separate threads.
  *  This is true for both shallow and deep clones.
  *  It is the responsibility of the Text Provider to ensure that this thread safety
  *  constraint is met.

  *
  *  @param dest   A UText struct to be filled in with the result of the clone operation,
  *                or NULL if the clone function should heap-allocate a new UText struct.
  *  @param src    The UText to be cloned.
  *  @param deep   true to request a deep clone, false for a shallow clone.
  *  @param status Errors are returned here.  For deep clones, U_UNSUPPORTED_ERROR
  *                should be returned if the text provider is unable to clone the
  *                original text.
  *  @return       The newly created clone, or NULL if the clone operation failed.
  *
  * @stable ICU 3.4
  */
static UText* U_CALLCONV icu_text_buffer_UTextClone(UText* dest, const UText* src, UBool deep, UErrorCode* status)
{
    if (U_FAILURE(*status)) {
        return NULL;
    }

    if (deep) {
        *status = U_UNSUPPORTED_ERROR;
        return dest;
    }

    dest = fn_utext_setup(dest, 4096, status);
    if (U_FAILURE(*status)) {
        return NULL;
    }

    dest->providerProperties = src->providerProperties;
    dest->chunkNativeLimit = src->chunkNativeLimit;
    dest->extraSize = src->extraSize;
    dest->nativeIndexingLimit = src->nativeIndexingLimit;
    dest->chunkNativeStart = src->chunkNativeStart;
    dest->chunkOffset = src->chunkOffset;
    dest->chunkLength = src->chunkLength;
    dest->chunkContents = dest->pExtra;
    dest->pFuncs = src->pFuncs;
    dest->context = src->context;
    dest->p = src->p;
    dest->q = src->q;
    dest->r = src->r;
    dest->a = src->a;
    dest->b = src->b;
    dest->c = src->c;
    memcpy(dest->pExtra, src->pExtra, 4096);
    return dest;
}

/**
 * Function type declaration for UText.nativeLength().
 *
 * @param ut the UText to get the length of.
 * @return the length, in the native units of the original text string.
 * @see UText
 * @stable ICU 3.4
 */
static int64_t U_CALLCONV icu_text_buffer_UTextNativeLength(UText* ut)
{
    TextBuffer* tb = (TextBuffer*)ut->context;
    return tb->text_length;
}

/**
 * Function type declaration for UText.access().  Get the description of the text chunk
 *  containing the text at a requested native index.  The UText's iteration
 *  position will be left at the requested index.  If the index is out
 *  of bounds, the iteration position will be left at the start or end
 *  of the string, as appropriate.
 *
 *  Chunks must begin and end on code point boundaries.  A single code point
 *  comprised of multiple storage units must never span a chunk boundary.
 *
 *
 * @param ut          the UText being accessed.
 * @param nativeIndex Requested index of the text to be accessed.
 * @param forward     If true, then the returned chunk must contain text
 *                    starting from the index, so that start<=index<limit.
 *                    If false, then the returned chunk must contain text
 *                    before the index, so that start<index<=limit.
 * @return            True if the requested index could be accessed.  The chunk
 *                    will contain the requested text.
 *                    False value if a chunk cannot be accessed
 *                    (the requested index is out of bounds).
 *
 * @see UText
 * @stable ICU 3.4
 */
static UBool U_CALLCONV icu_text_buffer_UTextAccess(UText* ut, int64_t nativeIndex, UBool forward)
{
    TextBuffer* tb = (TextBuffer*)ut->context;
    i64 index_contained = nativeIndex;

    if (!forward) {
        index_contained -= 1;
    }
    if (index_contained < 0 || (usize)index_contained >= tb->text_length) {
        return false;
    }

    if (index_contained >= ut->chunkNativeStart && index_contained < ut->chunkNativeLimit) {
        assert(false);
    }

    UErrorCode status = U_ZERO_ERROR;
    s8 text;
    i64 native_start;
    i64 native_limit;

    if (forward) {
        text = text_buffer_read_forward(tb, nativeIndex);
        text = s8_slice(text, 0, 2048);
        native_start = nativeIndex;
        native_limit = nativeIndex + text.len;
    } else {
        text = text_buffer_read_backward(tb, nativeIndex);
        text = s8_slice(text, text.len - min(text.len, 2048), text.len);
        native_start = nativeIndex - text.len;
        native_limit = nativeIndex;
    }

    // The utf16_buffer is 4096 bytes long = 2048 UChars.
    // The worst case scenario is that the text is ASCII (1 UChars per byte),
    // which means we can safely convert 2048 bytes to 2048 UChars.
    UChar* utf16_buffer = ut->pExtra;
    i32 utf16_buffer_len = 0;
    fn_u_strFromUTF8WithSub(utf16_buffer, 2048, &utf16_buffer_len, (const char*)text.beg, (i32)text.len, 0xfffd, NULL, &status);
    if (U_FAILURE(status)) {
        assert(false);
        return false;
    }

    ut->chunkContents = utf16_buffer;
    ut->chunkLength = utf16_buffer_len;
    ut->chunkOffset = forward ? 0 : utf16_buffer_len;
    ut->chunkNativeStart = native_start;
    ut->chunkNativeLimit = native_limit;
    ut->p = text.beg;
    ut->a = text.len;
    return true;
}

static int32_t U_CALLCONV icu_text_buffer_UTextExtract(UText* ut, int64_t nativeStart, int64_t nativeLimit, UChar* dest, int32_t destCapacity, UErrorCode* status)
{
    assert(false);
    return 0;
}

/**
 * Function type declaration for UText.replace().
 *
 * Replace a range of the original text with a replacement text.
 *
 * Leaves the current iteration position at the position following the
 *  newly inserted replacement text.
 *
 * This function need only be implemented on UText types that support writing.
 *
 * When using this function, there should be only a single UText opened onto the
 * underlying native text string.  The function is responsible for updating the
 * text chunk within the UText to reflect the updated iteration position,
 * taking into account any changes to the underlying string's structure caused
 * by the replace operation.
 *
 * @param ut               the UText representing the text to be operated on.
 * @param nativeStart      the index of the start of the region to be replaced
 * @param nativeLimit      the index of the character following the region to be replaced.
 * @param replacementText  pointer to the replacement text
 * @param replacmentLength length of the replacement text in UChars, or -1 if the text is NUL terminated.
 * @param status           receives any error status.  Possible errors include
 *                         U_NO_WRITE_PERMISSION
 *
 * @return The signed number of (native) storage units by which
 *         the length of the text expanded or contracted.
 *
 * @stable ICU 3.4
 */
static int32_t U_CALLCONV icu_text_buffer_UTextReplace(UText* ut, int64_t nativeStart, int64_t nativeLimit, const UChar* replacementText, int32_t replacmentLength, UErrorCode* status)
{
    TextBuffer* tb = (TextBuffer*)ut->context;

    if (nativeStart < 0 || (usize)nativeStart > tb->text_length) {
        return 0;
    }
    if (nativeLimit < nativeStart) {
        return 0;
    }

    if (replacmentLength == -1) {
        replacmentLength = fn_u_strlen(replacementText);
    }

    if ((usize)nativeLimit > tb->text_length) {
        nativeLimit = tb->text_length;
    }
    if (nativeStart == nativeLimit && replacmentLength == 0) {
        return 0;
    }

    assert(false); // TODO: Implement this function.
    return 0;
}

/**
 * Function type declaration for UText.mapOffsetToNative().
 * Map from the current UChar offset within the current text chunk to
 *  the corresponding native index in the original source text.
 *
 * This is required only for text providers that do not use native UTF-16 indexes.
 *
 * @param ut     the UText.
 * @return Absolute (native) index corresponding to chunkOffset in the current chunk.
 *         The returned native index should always be to a code point boundary.
 *
 * @stable ICU 3.4
 */
static int64_t U_CALLCONV icu_text_buffer_UTextMapOffsetToNative(const UText* ut)
{
    const c8* beg = ut->p;
    i64 len = ut->a;
    i64 native = 0;
    i32 offset = 0;

    while (offset < ut->chunkOffset) {
        u32 c;
        U8_NEXT_OR_FFFD(beg, native, len, c);
        offset += c >= 0x10000 ? 2 : 1;
    }

    assert(offset == ut->chunkOffset);
    return native + ut->chunkNativeStart;
}

/**
 * Function type declaration for UText.mapIndexToUTF16().
 * Map from a native index to a UChar offset within a text chunk.
 * Behavior is undefined if the native index does not fall within the
 *   current chunk.
 *
 * This function is required only for text providers that do not use native UTF-16 indexes.
 *
 * @param ut          The UText containing the text chunk.
 * @param nativeIndex Absolute (native) text index, chunk->start<=index<=chunk->limit.
 * @return            Chunk-relative UTF-16 offset corresponding to the specified native
 *                    index.
 *
 * @stable ICU 3.4
 */
static int32_t U_CALLCONV icu_text_buffer_UTextMapNativeIndexToUTF16(const UText* ut, int64_t nativeIndex)
{
    i32 length;
    UErrorCode status = U_ZERO_ERROR;
    i32 off = (i32)(nativeIndex - ut->chunkNativeStart);
    assert(off >= 0);
    fn_u_strFromUTF8WithSub(NULL, 0, &length, ut->p, off, 0xfffd, NULL, &status);
    return length;
}

static const UTextFuncs s_text_funcs = {
    .tableSize = sizeof(UTextFuncs),
    .clone = icu_text_buffer_UTextClone,
    .nativeLength = icu_text_buffer_UTextNativeLength,
    .access = icu_text_buffer_UTextAccess,
    .extract = icu_text_buffer_UTextExtract,
    .replace = icu_text_buffer_UTextReplace,
    .mapOffsetToNative = icu_text_buffer_UTextMapOffsetToNative,
    .mapNativeIndexToUTF16 = icu_text_buffer_UTextMapNativeIndexToUTF16,
};

void icu_init()
{
    if (s_icu_ok != -1) {
        return;
    }

    void* icu = os_load_library("icuuc.dll");
    if (icu == NULL) {
        return;
    }

    s_icu_ok = 1;

    OS_SUPPRESS_GET_PROC_NAGGING_BEGIN
    fn_utext_close = os_get_proc_address(icu, "utext_close");
    s_icu_ok &= fn_utext_close != NULL;
    fn_utext_setup = os_get_proc_address(icu, "utext_setup");
    s_icu_ok &= fn_utext_setup != NULL;
    fn_u_strlen = os_get_proc_address(icu, "u_strlen");
    s_icu_ok &= fn_u_strlen != NULL;
    fn_u_strToUTF8WithSub = os_get_proc_address(icu, "u_strToUTF8WithSub");
    s_icu_ok &= fn_u_strToUTF8WithSub != NULL;
    fn_u_strFromUTF8WithSub = os_get_proc_address(icu, "u_strFromUTF8WithSub");
    s_icu_ok &= fn_u_strFromUTF8WithSub != NULL;
    fn_ucnv_open = os_get_proc_address(icu, "ucnv_open");
    s_icu_ok &= fn_ucnv_open != NULL;
    fn_ucnv_close = os_get_proc_address(icu, "ucnv_close");
    s_icu_ok &= fn_ucnv_close != NULL;
    fn_ucnv_detectUnicodeSignature = os_get_proc_address(icu, "ucnv_detectUnicodeSignature");
    s_icu_ok &= fn_ucnv_detectUnicodeSignature != NULL;
    fn_ucnv_convertEx = os_get_proc_address(icu, "ucnv_convertEx");
    s_icu_ok &= fn_ucnv_convertEx != NULL;
    OS_SUPPRESS_GET_PROC_NAGGING_END
}

UText* text_buffer_utext(TextBuffer* tb)
{
    icu_init();
    if (!s_icu_ok) {
    }

    UErrorCode status = U_ZERO_ERROR;
    UText* ut = fn_utext_setup(NULL, 4096, &status);
    if (U_FAILURE(status)) {
        return NULL;
    }

    ut->providerProperties = 0; // TODO: (1 << UTEXT_PROVIDER_WRITABLE)
    ut->pFuncs = &s_text_funcs;
    ut->context = tb;
    return ut;
}

void text_buffer_utext_close(UText* ut)
{
    fn_utext_close(ut);
}

int read(char* buffer, usize length) { return -1; }

void write(const char* buffer, usize length) {}

const char* detectUnicodeSignature(const char* source, int32_t length)
{
    icu_init();

    UErrorCode status = U_ZERO_ERROR;
    const char* encoding = fn_ucnv_detectUnicodeSignature(source, length, NULL, &status);
    return U_SUCCESS(status) && encoding ? encoding : "UTF-8";
}

void convertToUTF8(const char* encoding)
{
    icu_init();

    UErrorCode errorCode = U_ZERO_ERROR;
    UConverter* sourceCnv = fn_ucnv_open(encoding, &errorCode);
    UConverter* targetCnv = fn_ucnv_open("UTF-8", &errorCode);
    if (U_FAILURE(errorCode)) {
        goto cleanup;
    }

    char sourceBuffer[1024];
    char targetBuffer[2048];
    UChar pivotBuffer[1024];
    const char* source;
    char* target;
    UChar* pivotSource = pivotBuffer;
    UChar* pivotTarget = pivotBuffer;
    const UChar* pivotLimit = pivotBuffer + 1024;
    bool reset = true;

    for (;;) {
        int bytesRead = read(sourceBuffer, sizeof(sourceBuffer));
        source = sourceBuffer;
        const char* sourceLimit = sourceBuffer + bytesRead;
        target = targetBuffer;
        char* targetLimit = targetBuffer + sizeof(targetBuffer);

        fn_ucnv_convertEx(targetCnv, sourceCnv, &target, targetLimit, &source, sourceLimit, pivotBuffer, &pivotSource, &pivotTarget, pivotLimit, reset, bytesRead <= 0, &errorCode);
        if (U_FAILURE(errorCode)) {
            break;
        }

        // TODO: Skip initial U+FEFF BOM?
        write(targetBuffer, target - targetBuffer);
        reset = false;

        if (bytesRead == 0) {
            break;
        }
    }

cleanup:
    fn_ucnv_close(sourceCnv);
    fn_ucnv_close(targetCnv);
}
