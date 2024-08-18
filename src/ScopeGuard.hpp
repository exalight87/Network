#pragma once
#include <functional>

class ScopeGuard
{
public:
	ScopeGuard() = delete;
	ScopeGuard(auto callable) : f(callable) {};
	~ScopeGuard() { f(); };

private:
	std::function< void(void) > f;
};