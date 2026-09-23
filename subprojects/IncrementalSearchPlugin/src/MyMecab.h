#ifndef MYMECAB_H
#define MYMECAB_H

#include <stdlib.h>
#include <vector>
#include <string>
#include <string_view>
#include "IncrementalSearch.h"

class MyMecab : public incrementalsearch::Engine {
private:
	struct Private;
	Private *m;
public:
public:
	MyMecab();
	~MyMecab() override;
	bool open(char const *dicpath) override;
	void close() override;
	std::vector<incrementalsearch::Part> parse(std::string_view const &line) const override;
	operator bool () const override;
};


#endif // MYMECAB_H
