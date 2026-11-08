#ifndef INCREMENTALSEARCHINTERFACE_H
#define INCREMENTALSEARCHINTERFACE_H

#include <QtPlugin>
#include "IncrementalSearch.h"

class IncrementalSearchInterface {
public:
	IncrementalSearchInterface();
	virtual ~IncrementalSearchInterface() {}
	virtual IncrementalSearch *create() = 0;
};

Q_DECLARE_INTERFACE(IncrementalSearchInterface, "soramimi.jp.IncrementalSearchPlugin/2.0")

#endif // INCREMENTALSEARCHINTERFACE_H
