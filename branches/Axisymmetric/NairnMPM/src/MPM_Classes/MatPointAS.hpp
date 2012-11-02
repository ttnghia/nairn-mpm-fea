/********************************************************************************
	MatPointAS.hpp
	NairnMPM

	Created by John Nairn on 10/23/12.
	Copyright (c) 2012 John A. Nairn, All rights reserved.

	Dependencies
		MatPoint2D.hpp, MPMBase.hpp
 ********************************************************************************/

#ifndef _MATPOINTAS_

#define _MATPOINTAS_

#include "MPM_Classes/MatPoint2D.hpp"

class MatPointAS : public MatPoint2D
{
	public:
	
		// constructors and destructors
		MatPointAS();
		MatPointAS(int,int,double,double);
	
		// methods
		virtual void SetOrigin(Vector *);
};

#endif
