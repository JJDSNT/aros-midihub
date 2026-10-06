#ifndef LIBRARIES_BONAMIBASE_H
#define LIBRARIES_BONAMIBASE_H
/*
 * bonamibase.h - bonami.library public base structure
 */

#ifndef EXEC_LIBRARIES_H
#include <exec/libraries.h>
#endif

#define BONAMINAME "bonami.library"

struct BonamiBase
{
    struct Library LibNode;
};

typedef APTR MDNSRegisterRef;
typedef APTR MDNSBrowseRef;
typedef APTR MDNSResolveRef;

#endif /* LIBRARIES_BONAMIBASE_H */
