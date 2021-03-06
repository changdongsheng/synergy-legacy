/*
 * synergy -- mouse and keyboard sharing utility
 * Copyright (C) 2004 Chris Schoeneman
 * 
 * This package is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * found in the file COPYING that should have accompanied this file.
 * 
 * This package is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef IARCHSYSTEM_H
#define IARCHSYSTEM_H

#include "IInterface.h"
#include "stdstring.h"

//! Interface for architecture dependent system queries
/*!
This interface defines operations for querying system info.
*/
class IArchSystem : public IInterface {
public:
	//! @name accessors
	//@{

	//! Identify the OS
	/*!
	Returns a string identifying the operating system.
	*/
	virtual std::string	getOSName() const = 0;

	//@}
};

#endif
