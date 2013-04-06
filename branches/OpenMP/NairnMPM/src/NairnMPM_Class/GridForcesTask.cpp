/******************************************************************************************
	GridForcesTask.cpp
	NairnMPM

	Created by John Nairn on July 22, 2010
	Copyright (c) 2010 John A. Nairn, All rights reserved.
 
	Find all forces on the grid including internal forces (from particle stress)
		external forces, and body forces. Add equivalent forces for transport
		tasks.
 
	After main loop, get traction forces on cracks, add crack tip heating,
		and imperfect interface forces on cracks and track interface energy.
 
	Sum all forces only with external damping on the nodes
 
	Finally reconcile forces with boundary conditions. Do same for transport
		tasks.
 
	Input Variables
		mpm[]->ncpos, sp, pFext
		nd[]->pk
		bdyFrc.damping
 
	Output Variables
		theMaterials->LoadTransportProperties() - changed if depend on particle state
		mvf[]: fint, fext, ftot
		nd[]->fdiff, fcond
		mpm[]->dispEnergy
******************************************************************************************/

#include "NairnMPM_Class/GridForcesTask.hpp"
#include "NairnMPM_Class/NairnMPM.hpp"
#include "Materials/MaterialBase.hpp"
#include "MPM_Classes/MPMBase.hpp"
#include "Custom_Tasks/TransportTask.hpp"
#include "Custom_Tasks/ConductionTask.hpp"
#include "Elements/ElementBase.hpp"
#include "Nodes/NodalPoint.hpp"
#include "Nodes/MaterialInterfaceNode.hpp"
#include "Global_Quantities/BodyForce.hpp"
#include "Cracks/CrackHeader.hpp"
#include "Cracks/CrackNode.hpp"
#include "Boundary_Conditions/NodalVelBC.hpp"
#include "Boundary_Conditions/MatPtTractionBC.hpp"
#ifdef LOG_PROGRESS
#include "System/ArchiveData.hpp"
#endif


#pragma mark CONSTRUCTORS

GridForcesTask::GridForcesTask(const char *name) : MPMTask(name)
{
	// zero this in case in 2D analysis
	for(int i=0;i<MaxShapeNds;i++) zDeriv[i]=0.;
}

#pragma mark REQUIRED METHODS

// Get total grid point forces (except external forces)
void GridForcesTask::Execute(void)
{
	// need to be private in threads
	TransportProperties t;
	
	int numnds,nds[MaxShapeNds];
	double xfrc,yfrc,zfrc,fn[MaxShapeNds],xDeriv[MaxShapeNds],yDeriv[MaxShapeNds];
	TransportTask *nextTransport;
	
	// loop over particles
    for(int p=0;p<nmpmsNR;p++)
	{	MPMBase *mpmptr=mpm[p];											// material point pointer
		const MaterialBase *matref = theMaterials[mpmptr->MatID()];		// material class (read only)
		
		// skip if material is rigid (and comes before last nonrigid one)
		if(matref->Rigid()) continue;
		
		// get transport tensors (if needed)
		if(transportTasks!=NULL)
			matref->GetTransportProps(mpmptr,fmobj->np,&t);
		
        double mp = mpmptr->mp;					// in g
		int matfld = matref->GetField();           // material field
		
        // find shape functions and derviatives
		const ElementBase *elemref = theElements[mpmptr->ElemID()];
 		elemref->GetShapeGradients(&numnds,fn,nds,mpmptr->GetNcpos(),xDeriv,yDeriv,zDeriv,mpmptr);
		
        // Add particle property to each node in the element
        for(int i=1;i<=numnds;i++)
		{	short vfld = (short)mpmptr->vfld[i];						// crack velocity field to use
			NodalPoint *ndptr = nd[nds[i]];								// nodal point pointer
			
            // internal force vector (in g mm/sec^2 or micro N)
			//	(note: stress is specific stress in units N/m^2 cm^3/g
			//	Multiply by 1000 to make it mm/sec^2)
			Vector theFrc;
			mpmptr->Fint(theFrc,xDeriv[i],yDeriv[i],zDeriv[i]);
			ndptr->AddFintTask3(vfld,matfld,&theFrc);
            
            // body forces (not 3D yet)
			if(bodyFrc.GetGravity(&xfrc,&yfrc,&zfrc))
            {   double gscale = mp*fn[i];
				theFrc = MakeVector(gscale*xfrc,gscale*yfrc,gscale*zfrc);
				ndptr->AddFintTask3(vfld,matfld,&theFrc);
			}
            
			// get external force vector and add to velocity field
			mpmptr->Fext(theFrc,fn[i]);
            ndptr->AddFextTask3(vfld,matfld,&theFrc);
			
			// transport forces
			nextTransport=transportTasks;
			while(nextTransport!=NULL)
				nextTransport=nextTransport->AddForces(ndptr,mpmptr,fn[i],xDeriv[i],yDeriv[i],zDeriv[i]);
        }
		
		// clear coupled dissipated energy if in conduction becaouse done with it this time step
		if(ConductionTask::active) mpmptr->SetDispEnergy(0.);
	}
	
	// Add traction BCs on particles
	MatPtTractionBC::SetParticleSurfaceTractions(mtime);
	
	// traction law forces add to fext in velocity fields
	if(fmobj->hasTractionCracks)
	{	CrackHeader *nextCrack=firstCrack;
		while(nextCrack!=NULL)
		{	nextCrack->TractionFext();
			nextCrack=(CrackHeader *)nextCrack->GetNextObject();
		}
	}
	
	// crack tip heating adds to fcond
	if(conduction) conduction->AddCrackTipHeating();
	
	// interface forces added to fint in velocity fields and track total interface energy
    NodalPoint::interfaceEnergy=0.;
    CrackNode::InterfaceOnKnownNodes();
    MaterialInterfaceNode::InterfaceOnKnownNodes();
    
	// Find grid total forces with external damping
	double damping = bodyFrc.GetDamping(mtime);		// could move inside loop and make function of nodal position too
    for(int i=1;i<=nnodes;i++)
		nd[i]->CalcFtotTask3(damping);
	
    // Imposed BCs on ftot to get correct grid BCs for velocity
    NodalVelBC::ConsistentGridForces();
	
	// Do similar to transport property BCs
	nextTransport=transportTasks;
	while(nextTransport!=NULL)
		nextTransport=nextTransport->SetTransportForceBCs(timestep);
    
}
