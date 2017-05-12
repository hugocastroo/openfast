#include "OpenFAST.H"

//Constructor 
fast::fastInputs::fastInputs():
nTurbinesGlob(0),
dryRun(false),
debug(false),
tStart(-1.0),
nEveryCheckPoint(-1),
tMax(0.0),
dtFAST(0.0),
scStatus(false),
scLibFile(""),
numScInputs(0),
numScOutputs(0)
{
  //Nothing to do here
}


//Constructor
fast::OpenFAST::OpenFAST():
nTurbinesGlob(0),
nTurbinesProc(0),
scStatus(false),
restart(false),
timeZero(false)
{
}

inline bool fast::OpenFAST::checkFileExists(const std::string& name) {
  struct stat buffer;   
  return (stat (name.c_str(), &buffer) == 0); 
}

void fast::OpenFAST::init() {

  allocateMemory();

  if (!dryRun) {
   // If restart 
   if (restart == true) {

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
       /* note that this will set nt_global inside the FAST library */
       FAST_OpFM_Restart(&iTurb, CheckpointFileRoot[iTurb].data(), &AbortErrLev, &dtFAST, &numBlades[iTurb], &numVelPtsBlade[iTurb], &ntStart, &cDriver_Input_from_FAST[iTurb], &cDriver_Output_to_FAST[iTurb], &cDriverSC_Input_from_FAST[iTurb], &cDriverSC_Output_to_FAST[iTurb], &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);
       nt_global = ntStart;
     }

     if(scStatus) {
	 sc->readRestartFile(nt_global);
     }

   
   } else {
     
      // this calls the Init() routines of each module

     forceNodeVel.resize(nTurbinesProc);
     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
       FAST_OpFM_Init(&iTurb, &tMax, FASTInputFileName[iTurb].data(), &TurbID[iTurb], &numScOutputs, &numScInputs, &numForcePtsBlade[iTurb], &numForcePtsTwr[iTurb], TurbineBasePos[iTurb].data(), &AbortErrLev, &dtFAST, &numBlades[iTurb], &numVelPtsBlade[iTurb], &cDriver_Input_from_FAST[iTurb], &cDriver_Output_to_FAST[iTurb], &cDriverSC_Input_from_FAST[iTurb], &cDriverSC_Output_to_FAST[iTurb], &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);
       
       timeZero = true;

       numVelPtsTwr[iTurb] = cDriver_Output_to_FAST[iTurb].u_Len - numBlades[iTurb]*numVelPtsBlade[iTurb] - 1;

       int nfPts = get_numForcePtsLoc(iTurb);
       forceNodeVel[iTurb].resize(nfPts) ;
       for (int k = 0; k < nfPts; k++) forceNodeVel[iTurb][k].resize(3) ;

       if ( isDebug() ) {
       	 for (int iNode=0; iNode < get_numVelPtsLoc(iTurb); iNode++) {
       	   std::cout << "Node " << iNode << " Position = " << cDriver_Input_from_FAST[iTurb].pxVel[iNode] << " " << cDriver_Input_from_FAST[iTurb].pyVel[iNode] << " " << cDriver_Input_from_FAST[iTurb].pzVel[iNode] << " " << std::endl ;
       	 }
       }
     }
     
   }
  }

}

void fast::OpenFAST::solution0() {

  if (!dryRun) {
       // set wind speeds at initial locations
    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
        setOutputsToFAST(cDriver_Input_from_FAST[iTurb], cDriver_Output_to_FAST[iTurb]);
    }
     
     if(scStatus) {

       sc->init(nTurbinesGlob, numScInputs, numScOutputs);

       sc->calcOutputs(scOutputsGlob);
       fillScOutputsLoc();
     }

     for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

       FAST_OpFM_Solution0(&iTurb, &ErrStat, ErrMsg);
       checkError(ErrStat, ErrMsg);

     }

     timeZero = false;

     if (scStatus) {
       fillScInputsGlob(); // Update inputs to super controller
     }
  }

}

void fast::OpenFAST::step() {

  /* ******************************
     set inputs from this code and call FAST:
  ********************************* */

   if(scStatus) {
     sc->calcOutputs(scOutputsGlob);
     fillScOutputsLoc();
   }

   for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {

     //  set wind speeds at original locations 
     setOutputsToFAST(cDriver_Input_from_FAST[iTurb], cDriver_Output_to_FAST[iTurb]);

     // this advances the states, calls CalcOutput, and solves for next inputs. Predictor-corrector loop is imbeded here:
     // (note OpenFOAM could do subcycling around this step)
     if ( isDebug() ) {
       for (int iNode=0; iNode < get_numVelPtsLoc(iTurb); iNode++) {
     	 std::cout << "Node " << iNode << " Velocity = " << cDriver_Output_to_FAST[iTurb].u[iNode] << " " << cDriver_Output_to_FAST[iTurb].v[iNode] << " " << cDriver_Output_to_FAST[iTurb].w[iNode] << " " << std::endl ;
       }
     }

     FAST_OpFM_Step(&iTurb, &ErrStat, ErrMsg);
     checkError(ErrStat, ErrMsg);

     if ( isDebug() ) {
       for (int iNode=0; iNode < get_numForcePtsLoc(iTurb); iNode++) {
	 std::cout << "Node " << iNode << " Position = " << cDriver_Input_from_FAST[iTurb].pxForce[iNode] << " " << cDriver_Input_from_FAST[iTurb].pyForce[iNode] << " " << cDriver_Input_from_FAST[iTurb].pzForce[iNode] << " " << std::endl ;
       }
       std::ofstream actuatorForcesFile;
       actuatorForcesFile.open("actuatorForces.csv") ;
       actuatorForcesFile << "# x, y, z, fx, fy, fz" << std::endl ;
       for (int iNode=0; iNode < get_numForcePtsLoc(iTurb); iNode++) {
	 actuatorForcesFile << cDriver_Input_from_FAST[iTurb].pxForce[iNode] << ", " << cDriver_Input_from_FAST[iTurb].pyForce[iNode] << ", " << cDriver_Input_from_FAST[iTurb].pzForce[iNode] << ", " << cDriver_Input_from_FAST[iTurb].fx[iNode] << ", " << cDriver_Input_from_FAST[iTurb].fy[iNode] << ", " << cDriver_Input_from_FAST[iTurb].fz[iNode] << " " << std::endl ;           
       }
       actuatorForcesFile.close() ;
     }

   }

   if(scStatus) {
     sc->updateStates(scInputsGlob); // Go from 'n' to 'n+1' based on input at previous time step
     fillScInputsGlob(); // Update inputs to super controller for 'n+1'
   }

   nt_global = nt_global + 1;
  
  if ( (((nt_global - ntStart) % nEveryCheckPoint) == 0 )  && (nt_global != ntStart) ) {
    //sprintf(CheckpointFileRoot, "../../CertTest/Test18.%d", nt_global);
    for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
      CheckpointFileRoot[iTurb] = " "; // if blank, it will use FAST convention <RootName>.nt_global
      FAST_CreateCheckpoint(&iTurb, CheckpointFileRoot[iTurb].data(), &ErrStat, ErrMsg);
      checkError(ErrStat, ErrMsg);
    }
    if(scStatus) {
      if (fastMPIRank == 0) {
          sc->writeRestartFile(nt_global);
      }
    }
  }

}

void fast::OpenFAST::setInputs(const fast::fastInputs & fi ) {


  mpiComm = fi.comm;

  MPI_Comm_rank(mpiComm, &worldMPIRank);
  MPI_Comm_group(mpiComm, &worldMPIGroup);

  nTurbinesGlob = fi.nTurbinesGlob;

    if (nTurbinesGlob > 0) {
      
      dryRun = fi.dryRun;
      
      debug = fi.debug;

      tStart = fi.tStart;
      nEveryCheckPoint = fi.nEveryCheckPoint;
      tMax = fi.tMax;
      loadSuperController(fi);
      dtFAST = fi.dtFAST;
      
      ntStart = int(tStart/dtFAST);
      
      nt_global = ntStart;
      if (ntStart > 0) {
	restart = true ;
      } 

      globTurbineData.resize(nTurbinesGlob);
      globTurbineData = fi.globTurbineData;

    } else {
      throw std::runtime_error("Number of turbines < 0 ");
    }
    
}

void fast::OpenFAST::checkError(const int ErrStat, const char * ErrMsg){

   if (ErrStat != ErrID_None){

      if (ErrStat >= AbortErrLev){
	throw std::runtime_error(ErrMsg);
      }

   }

}

void fast::OpenFAST::setOutputsToFAST(OpFM_InputType_t cDriver_Input_from_FAST, OpFM_OutputType_t cDriver_Output_to_FAST){

   // routine sets the u-v-w wind speeds used in FAST and the SuperController inputs

   for (int j = 0; j < cDriver_Output_to_FAST.u_Len; j++){
      cDriver_Output_to_FAST.u[j] = (float) 10.0*pow((cDriver_Input_from_FAST.pzVel[j] / 90.0), 0.2); // 0.2 power law wind profile using reference 10 m/s at 90 meters
      cDriver_Output_to_FAST.v[j] = 0.0;
      cDriver_Output_to_FAST.w[j] = 0.0;
   }

   // call supercontroller
   for (int j = 0; j < cDriver_Output_to_FAST.SuperController_Len; j++){
      cDriver_Output_to_FAST.SuperController[j] = (float) j; // set it somehow.... (would be set from the SuperController outputs)
   }

}

void fast::OpenFAST::getHubPos(std::vector<double> & currentCoords, int iTurbGlob) {

  // Get hub position of Turbine 'iTurbGlob'
  currentCoords[0] = globTurbineData[iTurbGlob].TurbineHubPos[0] ;
  currentCoords[1] = globTurbineData[iTurbGlob].TurbineHubPos[1] ;
  currentCoords[2] = globTurbineData[iTurbGlob].TurbineHubPos[2] ;
  
}

void fast::OpenFAST::getHubShftDir(std::vector<double> & hubShftVec, int iTurbGlob) {

  // Get hub shaft direction of current turbine - pointing downwind
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  hubShftVec[0] = cDriver_Input_from_FAST[iTurbLoc].pOrientation[0] ;
  hubShftVec[1] = cDriver_Input_from_FAST[iTurbLoc].pOrientation[3] ;
  hubShftVec[2] = cDriver_Input_from_FAST[iTurbLoc].pOrientation[6] ;

}


void fast::OpenFAST::getVelNodeCoordinates(std::vector<double> & currentCoords, int iNode, int iTurbGlob) {

  // Set coordinates at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbLoc);
  currentCoords[0] = cDriver_Input_from_FAST[iTurbLoc].pxVel[iNode] ;
  currentCoords[1] = cDriver_Input_from_FAST[iTurbLoc].pyVel[iNode] ;
  currentCoords[2] = cDriver_Input_from_FAST[iTurbLoc].pzVel[iNode] ;
  
}

void fast::OpenFAST::getForceNodeCoordinates(std::vector<double> & currentCoords, int iNode, int iTurbGlob) {

  // Set coordinates at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  currentCoords[0] = cDriver_Input_from_FAST[iTurbLoc].pxForce[iNode] ;
  currentCoords[1] = cDriver_Input_from_FAST[iTurbLoc].pyForce[iNode] ;
  currentCoords[2] = cDriver_Input_from_FAST[iTurbLoc].pzForce[iNode] ;

}

void fast::OpenFAST::getForceNodeOrientation(std::vector<double> & currentOrientation, int iNode, int iTurbGlob) {

  // Set orientation at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  for(int i=0;i<9;i++) {
    currentOrientation[i] = cDriver_Input_from_FAST[iTurbLoc].pxForce[iNode*9+i] ;
  }

}

void fast::OpenFAST::getForce(std::vector<double> & currentForce, int iNode, int iTurbGlob) {

  // Set forces at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  currentForce[0] = -cDriver_Input_from_FAST[iTurbLoc].fx[iNode] ;
  currentForce[1] = -cDriver_Input_from_FAST[iTurbLoc].fy[iNode] ;
  currentForce[2] = -cDriver_Input_from_FAST[iTurbLoc].fz[iNode] ;

}

double fast::OpenFAST::getChord(int iNode, int iTurbGlob) {

  // Return blade chord/tower diameter at current node of current turbine 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbLoc);
  return cDriver_Input_from_FAST[iTurbLoc].forceNodesChord[iNode] ;

}

void fast::OpenFAST::setVelocity(std::vector<double> & currentVelocity, int iNode, int iTurbGlob) {

  // Set velocity at current node of current turbine - 
  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbLoc);
  cDriver_Output_to_FAST[iTurbLoc].u[iNode] = currentVelocity[0];
  cDriver_Output_to_FAST[iTurbLoc].v[iNode] = currentVelocity[1];
  cDriver_Output_to_FAST[iTurbLoc].w[iNode] = currentVelocity[2];

}

void fast::OpenFAST::interpolateVel_ForceToVelNodes() {

  // Interpolates the velocity from the force nodes to the velocity nodes
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    // Hub location
    cDriver_Output_to_FAST[iTurb].u[0] = forceNodeVel[iTurb][0][0];
    cDriver_Output_to_FAST[iTurb].v[0] = forceNodeVel[iTurb][0][1];
    cDriver_Output_to_FAST[iTurb].w[0] = forceNodeVel[iTurb][0][2];

    // Do the blades first
    int nBlades = get_numBladesLoc(iTurb);
    for(int iBlade=0; iBlade < nBlades; iBlade++) {

      // Create interpolating parameter - Distance from hub
      int nForcePtsBlade = get_numForcePtsBladeLoc(iTurb);
      std::vector<double> rDistForce(nForcePtsBlade) ;
      for(int j=0; j < nForcePtsBlade; j++) {
	int iNodeForce = 1 + iBlade * nForcePtsBlade + j ; //The number of actuator force points is always the same for all blades
	rDistForce[j] = sqrt( 
                             (cDriver_Input_from_FAST[iTurb].pxForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pxForce[0])*(cDriver_Input_from_FAST[iTurb].pxForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pxForce[0])  
		           + (cDriver_Input_from_FAST[iTurb].pyForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pyForce[0])*(cDriver_Input_from_FAST[iTurb].pyForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pyForce[0])  
		           + (cDriver_Input_from_FAST[iTurb].pzForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pzForce[0])*(cDriver_Input_from_FAST[iTurb].pzForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pzForce[0])  			
			    );
      }

      // Interpolate to the velocity nodes
      int nVelPtsBlade = get_numVelPtsBladeLoc(iTurb);
      for(int j=0; j < nVelPtsBlade; j++) {
	int iNodeVel = 1 + iBlade * nVelPtsBlade + j ; //Assumes the same number of velocity (Aerodyn) nodes for all blades
	double rDistVel = sqrt( 
			      (cDriver_Input_from_FAST[iTurb].pxVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pxVel[0])*(cDriver_Input_from_FAST[iTurb].pxVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pxVel[0])  
		            + (cDriver_Input_from_FAST[iTurb].pyVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pyVel[0])*(cDriver_Input_from_FAST[iTurb].pyVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pyVel[0])  
		            + (cDriver_Input_from_FAST[iTurb].pzVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pzVel[0])*(cDriver_Input_from_FAST[iTurb].pzVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pzVel[0])  			
			      );
	//Find nearest two force nodes
	int jForceLower = 0;
	while (  ((rDistForce[jForceLower]-rDistVel)*(rDistForce[jForceLower+1]-rDistVel) >  0) && (jForceLower < (nForcePtsBlade-1)  ) ) {
	  jForceLower = jForceLower + 1;
	}
	int iNodeForceLower = 1 + iBlade * nForcePtsBlade + jForceLower ; 
	double rInterp = (rDistVel - rDistForce[jForceLower])/(rDistForce[jForceLower+1]-rDistForce[jForceLower]);
	cDriver_Output_to_FAST[iTurb].u[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][0] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][0] - forceNodeVel[iTurb][iNodeForceLower][0] );
	cDriver_Output_to_FAST[iTurb].v[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][1] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][1] - forceNodeVel[iTurb][iNodeForceLower][1] );
	cDriver_Output_to_FAST[iTurb].w[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][2] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][2] - forceNodeVel[iTurb][iNodeForceLower][2] );
      }
    }

    // Now the tower if present and used
    int nVelPtsTower = get_numVelPtsTwrLoc(iTurb);
    if ( nVelPtsTower > 0 ) {

      // Create interpolating parameter - Distance from first node from ground
      int nForcePtsTower = get_numForcePtsTwrLoc(iTurb);
      std::vector<double> hDistForce(nForcePtsTower) ;
      int iNodeBotTowerForce = 1 + nBlades * get_numForcePtsBladeLoc(iTurb); // The number of actuator force points is always the same for all blades
      for(int j=0; j < nForcePtsTower; j++) {
	int iNodeForce = iNodeBotTowerForce + j ; 
	hDistForce[j] = sqrt( 
			     (cDriver_Input_from_FAST[iTurb].pxForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pxForce[iNodeBotTowerForce])*(cDriver_Input_from_FAST[iTurb].pxForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pxForce[iNodeBotTowerForce])  
                           + (cDriver_Input_from_FAST[iTurb].pyForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pyForce[iNodeBotTowerForce])*(cDriver_Input_from_FAST[iTurb].pyForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pyForce[iNodeBotTowerForce])  
			   + (cDriver_Input_from_FAST[iTurb].pzForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pzForce[iNodeBotTowerForce])*(cDriver_Input_from_FAST[iTurb].pzForce[iNodeForce] - cDriver_Input_from_FAST[iTurb].pzForce[iNodeBotTowerForce])	
			    );
      }
      
      
      int iNodeBotTowerVel = 1 + nBlades * get_numVelPtsBladeLoc(iTurb); // Assumes the same number of velocity (Aerodyn) nodes for all blades
      for(int j=0; j < nVelPtsTower; j++) {
	int iNodeVel = iNodeBotTowerVel + j ; 
	double hDistVel = sqrt( 
			       (cDriver_Input_from_FAST[iTurb].pxVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pxVel[iNodeBotTowerVel])*(cDriver_Input_from_FAST[iTurb].pxVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pxVel[iNodeBotTowerVel])  
                             + (cDriver_Input_from_FAST[iTurb].pyVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pyVel[iNodeBotTowerVel])*(cDriver_Input_from_FAST[iTurb].pyVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pyVel[iNodeBotTowerVel])  
                             + (cDriver_Input_from_FAST[iTurb].pzVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pzVel[iNodeBotTowerVel])*(cDriver_Input_from_FAST[iTurb].pzVel[iNodeVel] - cDriver_Input_from_FAST[iTurb].pzVel[iNodeBotTowerVel])  			
	                      );
	//Find nearest two force nodes
	int jForceLower = 0;
	while (  ((hDistForce[jForceLower]-hDistVel)*(hDistForce[jForceLower+1]-hDistVel) >  0) && (jForceLower < (nForcePtsTower-1)  ) ) {
	  jForceLower = jForceLower + 1;
	}
	int iNodeForceLower = iNodeBotTowerForce + jForceLower ; 
	double rInterp = (hDistVel - hDistForce[jForceLower])/(hDistForce[jForceLower+1]-hDistForce[jForceLower]);
	cDriver_Output_to_FAST[iTurb].u[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][0] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][0] - forceNodeVel[iTurb][iNodeForceLower][0] );
	cDriver_Output_to_FAST[iTurb].v[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][1] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][1] - forceNodeVel[iTurb][iNodeForceLower][1] );
	cDriver_Output_to_FAST[iTurb].w[iNodeVel] = forceNodeVel[iTurb][iNodeForceLower][2] + rInterp * (forceNodeVel[iTurb][iNodeForceLower+1][2] - forceNodeVel[iTurb][iNodeForceLower][2] );
      }
    }    
    
  }
  
}

void fast::OpenFAST::computeTorqueThrust(int iTurbGlob, std::vector<double> & torque, std::vector<double> & thrust) {

    //Compute the torque and thrust based on the forces at the actuator nodes
    double relLoc[] = {0.0,0.0,0.0} ;
    thrust[0] = 0.0; thrust[1] = 0.0; thrust[2] = 0.0;
    torque[0] = 0.0; torque[1] = 0.0; torque[2] = 0.0;    
    
    int iTurbLoc = get_localTurbNo(iTurbGlob) ;
    for (int k=0; k < get_numBladesLoc(iTurbLoc); k++) {
        for (int j=0; j < numForcePtsBlade[iTurbLoc]; j++) {
            int iNode = 1 + numForcePtsBlade[iTurbLoc]*k + j ;
            
            thrust[0] = thrust[0] + cDriver_Input_from_FAST[iTurbLoc].fx[iNode] ;
            thrust[1] = thrust[1] + cDriver_Input_from_FAST[iTurbLoc].fy[iNode] ;
            thrust[2] = thrust[2] + cDriver_Input_from_FAST[iTurbLoc].fz[iNode] ;

            relLoc[0] = cDriver_Input_from_FAST[iTurbLoc].pxForce[iNode] - cDriver_Input_from_FAST[iTurbLoc].pxForce[0] ;
            relLoc[1] = cDriver_Input_from_FAST[iTurbLoc].pyForce[iNode] - cDriver_Input_from_FAST[iTurbLoc].pyForce[0];
            relLoc[2] = cDriver_Input_from_FAST[iTurbLoc].pzForce[iNode] - cDriver_Input_from_FAST[iTurbLoc].pzForce[0];            

            torque[0] = torque[0] + relLoc[1] * cDriver_Input_from_FAST[iTurbLoc].fz[iNode] - relLoc[2] * cDriver_Input_from_FAST[iTurbLoc].fy[iNode] + cDriver_Input_from_FAST[iTurbLoc].momentx[iNode] ;
            torque[1] = torque[1] + relLoc[2] * cDriver_Input_from_FAST[iTurbLoc].fx[iNode] - relLoc[0] * cDriver_Input_from_FAST[iTurbLoc].fz[iNode] + cDriver_Input_from_FAST[iTurbLoc].momenty[iNode] ;
            torque[2] = torque[2] + relLoc[0] * cDriver_Input_from_FAST[iTurbLoc].fy[iNode] - relLoc[1] * cDriver_Input_from_FAST[iTurbLoc].fx[iNode] + cDriver_Input_from_FAST[iTurbLoc].momentz[iNode] ;
            
        }
    }
}

fast::ActuatorNodeType fast::OpenFAST::getVelNodeType(int iTurbGlob, int iNode) {
  // Return the type of velocity node for the given node number. The node ordering (from FAST) is 
  // Node 0 - Hub node
  // Blade 1 nodes
  // Blade 2 nodes
  // Blade 3 nodes
  // Tower nodes

  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numVelPtsLoc(iTurbGlob);
  if (iNode) {
    if ( (iNode + 1 - (get_numVelPts(iTurbLoc) - get_numVelPtsTwr(iTurbLoc)) ) > 0) {
      return TOWER; 
    }
    else {
      return BLADE;
    }
  }
  else {
    return HUB; 
  }
  
}

fast::ActuatorNodeType fast::OpenFAST::getForceNodeType(int iTurbGlob, int iNode) {
  // Return the type of actuator force node for the given node number. The node ordering (from FAST) is 
  // Node 0 - Hub node
  // Blade 1 nodes
  // Blade 2 nodes
  // Blade 3 nodes
  // Tower nodes

  int iTurbLoc = get_localTurbNo(iTurbGlob);
  for(int j=0; j < iTurbLoc; j++) iNode = iNode - get_numForcePtsLoc(iTurbGlob);
  if (iNode) {
    if ( (iNode + 1 - (get_numForcePts(iTurbLoc) - get_numForcePtsTwr(iTurbLoc)) ) > 0) {
      return TOWER; 
    }
    else {
      return BLADE;
    }
  }
  else {
    return HUB; 
  }
  
}

void fast::OpenFAST::allocateMemory() {
	
  for (int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
    if (dryRun) {
      if(worldMPIRank == 0) {
	std::cout << "iTurb = " << iTurb << " turbineMapGlobToProc[iTurb] = " << turbineMapGlobToProc[iTurb] << std::endl ;
      }
    }
    if(worldMPIRank == turbineMapGlobToProc[iTurb]) {
      turbineMapProcToGlob[nTurbinesProc] = iTurb;
      reverseTurbineMapProcToGlob[iTurb] = nTurbinesProc;
      nTurbinesProc++ ;
    }
    turbineSetProcs.insert(turbineMapGlobToProc[iTurb]);
  }

  int nProcsWithTurbines=0;
  turbineProcs.resize(turbineSetProcs.size());
  std::ofstream turbineAllocFile;
  turbineAllocFile.open("turbineAlloc." + std::to_string(worldMPIRank) + ".txt") ;
  for (std::set<int>::const_iterator p = turbineSetProcs.begin(); p != turbineSetProcs.end(); p++) {
    turbineProcs[nProcsWithTurbines] = *p;

    if (dryRun) {
      if ( worldMPIRank == turbineProcs[nProcsWithTurbines] ) {
	for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
	  turbineAllocFile << "Proc " << worldMPIRank << " loc iTurb " << iTurb << " glob iTurb " << turbineMapProcToGlob[iTurb] << std::endl ;
	}
      }
    }

    nProcsWithTurbines++ ;
  }
  turbineAllocFile.flush();
  turbineAllocFile.close() ;
    
  // Construct a group containing all procs running atleast 1 turbine in FAST
  MPI_Group_incl(worldMPIGroup, nProcsWithTurbines, &turbineProcs[0], &fastMPIGroup) ;
  int fastMPIcommTag = MPI_Comm_create(mpiComm, fastMPIGroup, &fastMPIComm);
  if (MPI_COMM_NULL != fastMPIComm) {
    MPI_Comm_rank(fastMPIComm, &fastMPIRank);
  }

  TurbID.resize(nTurbinesProc);
  TurbineBasePos.resize(nTurbinesProc);
  FASTInputFileName.resize(nTurbinesProc);
  CheckpointFileRoot.resize(nTurbinesProc);
  numBlades.resize(nTurbinesProc);
  numForcePtsBlade.resize(nTurbinesProc);
  numForcePtsTwr.resize(nTurbinesProc);
  numVelPtsBlade.resize(nTurbinesProc);
  numVelPtsTwr.resize(nTurbinesProc);
  
  for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    
    TurbineBasePos[iTurb].resize(3);

    int globProc = turbineMapProcToGlob[iTurb];
    TurbID[iTurb] = globTurbineData[globProc].TurbID;
    FASTInputFileName[iTurb] = globTurbineData[globProc].FASTInputFileName ;
    CheckpointFileRoot[iTurb] = globTurbineData[globProc].FASTRestartFileName ;
    for(int i=0;i<3;i++) {
      TurbineBasePos[iTurb][i] = globTurbineData[globProc].TurbineBasePos[i];
    }
    numForcePtsBlade[iTurb] = globTurbineData[globProc].numForcePtsBlade;
    numForcePtsTwr[iTurb] = globTurbineData[globProc].numForcePtsTwr;

  }

  // Allocate memory for Turbine datastructure for all turbines
  FAST_AllocateTurbines(&nTurbinesProc, &ErrStat, ErrMsg);
  
  // Allocate memory for OpFM Input types in FAST
  cDriver_Input_from_FAST.resize(nTurbinesProc) ;
  cDriver_Output_to_FAST.resize(nTurbinesProc) ;
  
  cDriverSC_Input_from_FAST.resize(nTurbinesProc) ;
  cDriverSC_Output_to_FAST.resize(nTurbinesProc) ;

}

// void fast::OpenFAST::readTurbineData(int iTurb, YAML::Node turbNode) {

//   //Read turbine data for a given turbine using the YAML node
//   if (turbNode["turb_id"]) {
//     globTurbineData[iTurb].TurbID = turbNode["turb_id"].as<int>();
//   } else {
//     std::cerr << "turb_id not present for Turbine " << iTurb << " in the input file" << std::endl;
//   }

//   if (turbNode["FAST_input_filename"]) {
//     globTurbineData[iTurb].FASTInputFileName = turbNode["FAST_input_filename"].as<std::string>();
//   } else {
//     std::cerr << "FAST_input_filename not present for Turbine " << iTurb << " in the input file" << std::endl;
//   }

//   if (turbNode["restart_filename"]) {
//     globTurbineData[iTurb].FASTRestartFileName = turbNode["restart_filename"].as<std::string>();
//   } else {
//     std::cerr << " not present for Turbine " << iTurb << " in the input file" << std::endl;
//   }

//   if (turbNode["turbine_base_pos"].IsSequence() ) {
//     globTurbineData[iTurb].TurbineBasePos = turbNode["turbine_base_pos"].as<std::vector<double> >() ;
//   } else {
//     std::cerr << "turbine_base_pos not present for Turbine " << iTurb << " in the input file" << std::endl;
//   }

//   if (turbNode["turbine_hub_pos"].IsSequence() ) {
//     globTurbineData[iTurb].TurbineHubPos = turbNode["turbine_hub_pos"].as<std::vector<double> >() ;
//   } else {
//     std::cerr << "turbine_hub_pos not present for Turbine " << iTurb << " in the input file" << std::endl;
//   }

//   if (turbNode["num_force_pts_blade"]) {
//     globTurbineData[iTurb].numForcePtsBlade = turbNode["num_force_pts_blade"].as<int>();
//   } else {
//     std::cout << "num_force_pts_blade not present for Turbine " << iTurb << " in the input file" << std::endl;
//     std::cout << "  ... proceeding anyway. Will fail if you want CFD integration" << std::endl ;
//   }

//   if (turbNode["num_force_pts_tower"]) {
//     globTurbineData[iTurb].numForcePtsTwr = turbNode["num_force_pts_tower"].as<int>();
//   } else {
//     std::cout << "num_force_pts_tower not present for Turbine " << iTurb << " in the input file" << std::endl;
//     std::cout << "  ... proceeding anyway. Will fail if you want CFD integration" << std::endl ;
//   }

//   return ;
// }


void fast::OpenFAST::allocateTurbinesToProcsSimple() {
  
  // Allocate turbines to each processor - round robin fashion
  int nProcs ;
  MPI_Comm_size(mpiComm, &nProcs);
  for(int j = 0; j < nTurbinesGlob; j++)  turbineMapGlobToProc[j] = j % nProcs ;
  
}

void fast::OpenFAST::end() {

    // Deallocate types we allocated earlier
  
    if ( !dryRun) {
      bool stopTheProgram = false;
      for (int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
	FAST_End(&iTurb, &stopTheProgram);
      }
    }

    MPI_Group_free(&fastMPIGroup);
    if (MPI_COMM_NULL != fastMPIComm) {
      MPI_Comm_free(&fastMPIComm);
    }
    MPI_Group_free(&worldMPIGroup);

    if(scStatus) {

      destroy_SuperController(sc) ;

      if(scLibHandle != NULL) {
	// close the library
	std::cout << "Closing library...\n";
	dlclose(scLibHandle);
      }
      
    }

  }


void fast::OpenFAST::loadSuperController(const fast::fastInputs & fi) {

  if(fi.scStatus) {

    scStatus = fi.scStatus;
    scLibFile = fi.scLibFile;

    // open the library
    scLibHandle = dlopen(scLibFile.c_str(), RTLD_LAZY);
    if (!scLibHandle) {
      std::cerr << "Cannot open library: " << dlerror() << '\n';
    }
    
    create_SuperController = (create_sc_t*) dlsym(scLibHandle, "create_sc");
    // reset errors
    dlerror();
    const char *dlsym_error = dlerror();
    if (dlsym_error) {
      std::cerr << "Cannot load symbol 'create_sc': " << dlsym_error << '\n';
      dlclose(scLibHandle);
    }

    destroy_SuperController = (destroy_sc_t*) dlsym(scLibHandle, "destroy_sc");
    // reset errors
    dlerror();
    const char *dlsym_error_us = dlerror();
    if (dlsym_error_us) {
      std::cerr << "Cannot load symbol 'destroy_sc': " << dlsym_error_us << '\n';
      dlclose(scLibHandle);
    }

    sc = create_SuperController() ;

    numScInputs = fi.numScInputs;
    numScOutputs = fi.numScOutputs;

    if ( (numScInputs > 0) && (numScOutputs > 0)) {
      scOutputsGlob.resize(nTurbinesGlob*numScOutputs) ;
      scInputsGlob.resize(nTurbinesGlob*numScInputs) ;
      for (int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
	for(int iInput=0; iInput < numScInputs; iInput++) {
	  scInputsGlob[iTurb*numScInputs + iInput] = 0.0 ; // Initialize to zero
	}
	for(int iOutput=0; iOutput < numScOutputs; iOutput++) {
	  scOutputsGlob[iTurb*numScOutputs + iOutput] = 0.0 ; // Initialize to zero
	}
      }

    } else {
      std::cerr <<  "Make sure numScInputs and numScOutputs are greater than zero" << std::endl;
    }
    
   } else {
    scStatus = false;
    numScInputs = 0;
    numScOutputs = 0;
   }

}


void fast::OpenFAST::fillScInputsGlob() {
  
  // Fills the global array containing inputs to the supercontroller from all turbines

  for(int iTurb=0; iTurb < nTurbinesGlob; iTurb++) {
    for(int iInput=0; iInput < numScInputs; iInput++) {
      scInputsGlob[iTurb*numScInputs + iInput] = 0.0; // Initialize to zero 
    }
  }
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    for(int iInput=0; iInput < numScInputs; iInput++) {
      scInputsGlob[turbineMapProcToGlob[iTurb]*numScInputs + iInput] = cDriverSC_Input_from_FAST[iTurb].toSC[iInput] ;
    }
  }
  
  if (MPI_COMM_NULL != fastMPIComm) {
    MPI_Allreduce(MPI_IN_PLACE, scInputsGlob.data(), numScInputs*nTurbinesGlob, MPI_DOUBLE, MPI_SUM, fastMPIComm) ;
  }

}


void fast::OpenFAST::fillScOutputsLoc() {
  
  // Fills the local array containing outputs from the supercontroller to each turbine
  
  for(int iTurb=0; iTurb < nTurbinesProc; iTurb++) {
    for(int iOutput=0; iOutput < numScOutputs; iOutput++) {
      cDriverSC_Output_to_FAST[iTurb].fromSC[iOutput] = scOutputsGlob[turbineMapProcToGlob[iTurb]*numScOutputs + iOutput] ;
    }
  }

}










