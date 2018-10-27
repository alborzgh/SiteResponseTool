#include <vector>

#include "FEModel.h"

#include "Vector.h"
#include "Matrix.h"

#include "Node.h"
#include "Element.h"
#include "NDMaterial.h"
#include "SP_Constraint.h"
#include "MP_Constraint.h"
#include "LinearSeries.h"
#include "PathSeries.h"
#include "PathTimeSeries.h"
#include "LoadPattern.h"
#include "NodalLoad.h"
#include "AnalysisModel.h"
#include "CTestNormDispIncr.h"
#include "StaticAnalysis.h"
#include "DirectIntegrationAnalysis.h"
#include "EquiSolnAlgo.h"
#include "StaticIntegrator.h"
#include "TransientIntegrator.h"
#include "ConstraintHandler.h"
#include "RCM.h"
#include "DOF_Numberer.h"
#include "BandGenLinSolver.h"
#include "LinearSOE.h"
#include "NodeIter.h"
#include "ElementIter.h"
#include "DataFileStream.h"
#include "Recorder.h"
#include "UniaxialMaterial.h"


#include "SSPbrick.h"
#include "Brick.h"
#include "J2CyclicBoundingSurface.h"
#include "ElasticIsotropicMaterial.h"
#include "ElasticMaterial.h"
#include "NewtonRaphson.h"
#include "LoadControl.h"
#include "Newmark.h"
#include "PenaltyConstraintHandler.h"
#include "TransformationConstraintHandler.h"
#include "BandGenLinLapackSolver.h"
#include "BandGenLinSOE.h"
#include "GroundMotion.h"
#include "ImposedMotionSP.h"
#include "TimeSeriesIntegrator.h"
#include "MultiSupportPattern.h"
#include "UniformExcitation.h"
#include "VariableTimeStepDirectIntegrationAnalysis.h"
#include "NodeRecorder.h"
#include "ElementRecorder.h"
#include "ViscousMaterial.h"
#include "ZeroLength.h"

#include "Information.h"

#define PRINTDEBUG false

SiteResponseModel::SiteResponseModel() 
{

}

SiteResponseModel::SiteResponseModel(SiteLayering layering) :
	SRM_layering(layering)
{
	theDomain = new Domain();
	this->generateTotalStressModel();
}

SiteResponseModel::~SiteResponseModel() {
	if (theDomain != NULL)
		delete theDomain;
	theDomain = NULL;
}

int
SiteResponseModel::generateTotalStressModel()
{
	Vector zeroVec(3);
	zeroVec.Zero();

	std::vector<int> layerNumElems;
	std::vector<int> layerNumNodes;
	std::vector<double> layerElemSize;

	int numLayers = SRM_layering.getNumLayers();
	int numElems = 0;
	int numNodes = 0;
	for (int layerCount = 0; layerCount < numLayers - 1; ++layerCount)
	{
		double thisLayerThick = SRM_layering.getLayer(layerCount).getThickness();
		double thisLayerVS = SRM_layering.getLayer(layerCount).getShearVelocity();
		double thisLayerMinWL = thisLayerVS / MAX_FREQUENCY;

		thisLayerThick = (thisLayerThick < thisLayerMinWL) ? thisLayerMinWL : thisLayerThick;

		int thisLayerNumEle = NODES_PER_WAVELENGTH * static_cast<int>(thisLayerThick / thisLayerMinWL) - 1;

		layerNumElems.push_back(thisLayerNumEle);
		layerNumNodes.push_back(4 * (thisLayerNumEle + (layerCount == 0)));
		layerElemSize.push_back(thisLayerThick / thisLayerNumEle);

		numElems += thisLayerNumEle;
		numNodes += 4 * (thisLayerNumEle + (layerCount == numLayers - 2));

		if (PRINTDEBUG)
			opserr << "Layer " << SRM_layering.getLayer(layerCount).getName().c_str() << " : Num Elements = " << thisLayerNumEle
			   << "(" << thisLayerThick / thisLayerNumEle << "), "
			   << ", Num Nodes = " << 4 * (thisLayerNumEle + (layerCount == 0)) << endln;
	}

	// create the nodes
	Node* theNode;

	double yCoord = 0.0;
	int nCount = 0;
	for (int layerCount = numLayers - 2; layerCount > -1; --layerCount)
	{
		if (PRINTDEBUG)
			opserr << "layer : " << SRM_layering.getLayer(layerCount).getName().c_str() << " - Number of Elements = "
			<< layerNumElems[layerCount] << " - Number of Nodes = " << layerNumNodes[layerCount]
			<< " - Element Thickness = " << layerElemSize[layerCount] << endln;
		
		for (int nodeCount = 0; nodeCount < layerNumNodes[layerCount]; nodeCount += 4)
		{
			theNode = new Node(nCount + nodeCount + 1, 3, 0.0, yCoord, 0.0); theDomain->addNode(theNode);
			theNode = new Node(nCount + nodeCount + 2, 3, 0.0, yCoord, 1.0); theDomain->addNode(theNode);
			theNode = new Node(nCount + nodeCount + 3, 3, 1.0, yCoord, 1.0); theDomain->addNode(theNode);
			theNode = new Node(nCount + nodeCount + 4, 3, 1.0, yCoord, 0.0); theDomain->addNode(theNode);

			if (PRINTDEBUG)
			{
				opserr << "Node " << nCount + nodeCount + 1 << " - 0.0" << ", " << yCoord << ", 0.0" << endln;
				opserr << "Node " << nCount + nodeCount + 2 << " - 0.0" << ", " << yCoord << ", 1.0" << endln;
				opserr << "Node " << nCount + nodeCount + 3 << " - 1.0" << ", " << yCoord << ", 1.0" << endln;
				opserr << "Node " << nCount + nodeCount + 4 << " - 1.0" << ", " << yCoord << ", 0.0" << endln;
			}

			yCoord += layerElemSize[layerCount];
		}
		nCount += layerNumNodes[layerCount];
	}

	//int count = 0;
	//NodeIter& theNodeIter = theDomain->getNodes();
	//Node * thisNode;
	//while ((thisNode = theNodeIter()) != 0)
	//{
	//	count++;
	//	opserr << "Node " << thisNode->getTag() << " = " << thisNode->getCrds() << endln;
	//}

	// apply fixities
	SP_Constraint* theSP;
	ID theSPtoRemove(8);
	theSP = new SP_Constraint(1, 0, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(0) = theSP->getTag();
	theSP = new SP_Constraint(1, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(1, 2, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(1) = theSP->getTag();
	theSP = new SP_Constraint(2, 0, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(2) = theSP->getTag();
	theSP = new SP_Constraint(2, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(2, 2, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(3) = theSP->getTag();
	theSP = new SP_Constraint(3, 0, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(4) = theSP->getTag();
	theSP = new SP_Constraint(3, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(3, 2, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(5) = theSP->getTag();
	theSP = new SP_Constraint(4, 0, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(6) = theSP->getTag();
	theSP = new SP_Constraint(4, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(4, 2, 0.0, true); theDomain->addSP_Constraint(theSP); theSPtoRemove(7) = theSP->getTag();

	// apply equalDOF
	MP_Constraint* theMP;
	Matrix Ccr(3, 3); Ccr(0, 0) = 1.0; Ccr(1, 1) = 1.0; Ccr(2, 2) = 1.0;
	ID rcDOF(3); rcDOF(0) = 0; rcDOF(1) = 1; rcDOF(2) = 2;
	for (int nodeCount = 4; nodeCount < numNodes; nodeCount += 4)
	{
		theMP = new MP_Constraint(nodeCount + 1, nodeCount + 2, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);
		theMP = new MP_Constraint(nodeCount + 1, nodeCount + 3, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);
		theMP = new MP_Constraint(nodeCount + 1, nodeCount + 4, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);
	}

	// create the materials
	NDMaterial* theMat;
	SoilLayer theLayer;
	for (int layerCount = 0; layerCount < numLayers - 1; ++layerCount)
	{
		theLayer = (SRM_layering.getLayer(numLayers - layerCount - 2));
		theMat = new J2CyclicBoundingSurface(numLayers - layerCount - 1, theLayer.getMatShearModulus(), theLayer.getMatBulkModulus(),
			theLayer.getSu(), theLayer.getRho(), theLayer.getMat_h() * theLayer.getMatShearModulus(), theLayer.getMat_m(), 0.0, 0.5);
		//theMat = new ElasticIsotropicMaterial(numLayers - layerCount - 1, 20000.0, 0.3, theLayer.getRho());
		OPS_addNDMaterial(theMat);

		if (PRINTDEBUG)
			opserr << "Material " << theLayer.getName().c_str() << " tag = " << numLayers - layerCount - 1 << endln;
	}

	// create soil elements
	Element* theEle;
	int nElem = 0;

	for (int layerCount = 0; layerCount < numLayers - 1; ++layerCount)
	{
		theMat = OPS_getNDMaterial(numLayers - layerCount - 1);
		for (int elemCount = 0; elemCount < layerNumElems[numLayers - layerCount - 2]; ++elemCount)
		{
			int node1Tag = 4 * (nElem + elemCount);
			
			theEle = new SSPbrick(nElem + elemCount + 1, node1Tag + 1, node1Tag + 2, node1Tag + 3, node1Tag + 4, node1Tag + 5, 
				node1Tag + 6, node1Tag + 7, node1Tag + 8, *theMat, 0.0, -9.81 * theMat->getRho(), 0.0);
			theDomain->addElement(theEle);

			if (PRINTDEBUG)
				opserr << "Element " << nElem + elemCount + 1 << ": Nodes = " << node1Tag + 1 << " to " << node1Tag + 8 << "  - Mat tag = " << numLayers - layerCount - 1 << endln;
		}
		nElem += layerNumElems[numLayers - layerCount - 2];
	}

	if (PRINTDEBUG)
		opserr << "Total number of elements = " << nElem << endln;

	//int count = 0;
	//ElementIter& theEleIter = theDomain->getElements();
	//Element * thisEle;
	//while ((thisEle = theEleIter()) != 0)
	//{
	//	count++;
	//	opserr << "Element " << thisEle->getTag() << " = " << thisEle->getExternalNodes() << endln;
	//}

	// update material stage
	ElementIter& theEleIter = theDomain->getElements();
	Element * thisEle;
	Information matStage;
	matStage.setInt(0);
	while ((thisEle = theEleIter()) != 0)
	{
		thisEle->updateParameter(1, matStage);
	}


	AnalysisModel* theModel = new AnalysisModel();
	CTestNormDispIncr* theTest = new CTestNormDispIncr(1.0e-7, 30, 1);
	EquiSolnAlgo* theSolnAlgo = new NewtonRaphson(*theTest);
	StaticIntegrator* theIntegrator    = new LoadControl(0.05, 1, 0.05, 1.0);
	//TransientIntegrator* theIntegrator = new Newmark(0.5, 0.25);
	//ConstraintHandler* theHandler = new PenaltyConstraintHandler(1.0e14, 1.0e14);
	ConstraintHandler* theHandler = new TransformationConstraintHandler();
	RCM* theRCM = new RCM();
	DOF_Numberer* theNumberer = new DOF_Numberer(*theRCM);
	BandGenLinSolver* theSolver = new BandGenLinLapackSolver();
	LinearSOE* theSOE = new BandGenLinSOE(*theSolver);



	//DirectIntegrationAnalysis* theAnalysis;
	//theAnalysis = new DirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator, theTest);

	//VariableTimeStepDirectIntegrationAnalysis* theAnalysis;
	//theAnalysis = new VariableTimeStepDirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator, theTest);

	StaticAnalysis *theAnalysis;
	theAnalysis = new StaticAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator);
	theAnalysis->setConvergenceTest(*theTest);

	for (int analysisCount = 0; analysisCount < 2; ++analysisCount) {
		//int converged = theAnalysis->analyze(1, 0.01, 0.005, 0.02, 1);
		int converged = theAnalysis->analyze(1);
		if (!converged) {
			opserr << "Converged at time " << theDomain->getCurrentTime() << endln;
		}
	}

	// update material response to plastic
	theEleIter = theDomain->getElements();
	matStage.setInt(1);
	while ((thisEle = theEleIter()) != 0)
	{
		thisEle->updateParameter(1, matStage);
	}

	for (int analysisCount = 0; analysisCount < 2; ++analysisCount) {
		//int converged = theAnalysis->analyze(1, 0.01, 0.005, 0.02, 1);
		int converged = theAnalysis->analyze(1);
		if (!converged) {
			opserr << "Converged at time " << theDomain->getCurrentTime() << endln;
		}
	}

	// add the compliant base
	double vis_C = SRM_layering.getLayer(numLayers - 1).getShearVelocity() * SRM_layering.getLayer(numLayers - 1).getRho();
	UniaxialMaterial* theViscousMats[2];
	theViscousMats[0] = new ViscousMaterial(numLayers + 10, vis_C, 1.0); OPS_addUniaxialMaterial(theViscousMats[0]);
	theViscousMats[1] = new ViscousMaterial(numLayers + 20, vis_C, 1.0); OPS_addUniaxialMaterial(theViscousMats[1]);
	//theViscousMats[0] = new ElasticMaterial(1, 20000.0, 0.0); OPS_addUniaxialMaterial(theViscousMats[0]);
	//theViscousMats[1] = new ElasticMaterial(2, 20000.0, 0.0); OPS_addUniaxialMaterial(theViscousMats[1]);
	ID directions(2);
	directions(0) = 0; directions(1) = 2;

	theNode = new Node(numNodes + 1, 3, 0.0, 0.0, 0.0, NULL); theDomain->addNode(theNode);
	theNode = new Node(numNodes + 2, 3, 0.0, 0.0, 0.0, NULL); theDomain->addNode(theNode);
	theSP = new SP_Constraint(numNodes + 1, 0, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(numNodes + 1, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(numNodes + 1, 2, 0.0, true); theDomain->addSP_Constraint(theSP);
	//theSP = new SP_Constraint(numNodes + 2, 0, 0.0); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(numNodes + 2, 1, 0.0, true); theDomain->addSP_Constraint(theSP);
	theSP = new SP_Constraint(numNodes + 2, 2, 0.0); theDomain->addSP_Constraint(theSP);

	theMP = new MP_Constraint(1, numNodes + 2, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);

	theSP = theDomain->removeSP_Constraint(theSPtoRemove(0)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(1)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(2)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(3)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(4)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(5)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(6)); delete theSP;
	theSP = theDomain->removeSP_Constraint(theSPtoRemove(7)); delete theSP;
	
	Matrix constrainInXZ(2, 2); constrainInXZ(0, 0) = 1.0; constrainInXZ(1, 1) = 1.0;
	ID constDOF(2); constDOF(0) = 0; constDOF(1) = 2;
	
	theMP = new MP_Constraint(1, 2, constrainInXZ, constDOF, constDOF); theDomain->addMP_Constraint(theMP);
	theMP = new MP_Constraint(1, 3, constrainInXZ, constDOF, constDOF); theDomain->addMP_Constraint(theMP);
	theMP = new MP_Constraint(1, 4, constrainInXZ, constDOF, constDOF); theDomain->addMP_Constraint(theMP);

	Vector x(3); x(0) = 1.0; x(1) = 0.0; x(2) = 0.0;
	Vector y(3); y(1) = 1.0; y(0) = 0.0; y(2) = 0.0;
	theEle = new ZeroLength(numElems + 1, 3, numNodes + 1, numNodes + 2, x, y, 2, theViscousMats, directions);
	theDomain->addElement(theEle);


	// apply the motion

	PathTimeSeries* theTS_disp = NULL;
	PathTimeSeries* theTS_vel = new PathTimeSeries(1, "Motion1.vel", "Motion1.time", 1.0, true);
	PathTimeSeries* theTS_acc = new PathTimeSeries(1, "Motion1.acc", "Motion1.time", 9.81, true);

	GroundMotion* theMotion = new GroundMotion(theTS_disp, theTS_vel, theTS_acc);


	//MultiSupportPattern* theLP = new MultiSupportPattern(1);
	//theLP->addMotion(*theMotion, 1);
	
	//theLP->addSP_Constraint(new ImposedMotionSP(1, 0, 1, 1));
	//theLP->addSP_Constraint(new ImposedMotionSP(2, 0, 1, 1));
	//theLP->addSP_Constraint(new ImposedMotionSP(3, 0, 1, 1));
	//theLP->addSP_Constraint(new ImposedMotionSP(4, 0, 1, 1));

	LoadPattern* theLP = new LoadPattern(1, vis_C);
	theLP->setTimeSeries(theTS_vel);

	NodalLoad* theLoad;
	Vector load(3);
	load(0) = 1.0;
	load(1) = 0.0;
	load(2) = 0.0;
	theLoad = new NodalLoad(1, numNodes + 2, load, false); theLP->addNodalLoad(theLoad);

	// LoadPattern* theLP = new UniformExcitation(*theMotion, 1, 1, 0.0, 1.0);

	theDomain->addLoadPattern(theLP);



	delete theIntegrator;
	delete theAnalysis;

	TransientIntegrator* theTransientIntegrator = new Newmark(0.5, 0.25);
	theTest->setTolerance(1.0e-6);



	//DirectIntegrationAnalysis* theTransientAnalysis;
	//theTransientAnalysis = new DirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theTransientIntegrator, theTest);

	VariableTimeStepDirectIntegrationAnalysis* theTransientAnalysis;
	theTransientAnalysis = new VariableTimeStepDirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theTransientIntegrator, theTest);

	theDomain->setCurrentTime(0.0);

	// setup Rayleigh damping 
	double natFreq = SRM_layering.getNaturalPeriod();
	double dampRatio = 0.02;
	double pi = 4.0 * atan(1.0);
	double a0 = dampRatio * (10.0 * pi * natFreq) / 3.0 ;
	double a1 = dampRatio / (6.0 * pi * natFreq);
	if (PRINTDEBUG)
	{
		opserr << "f1 = " << natFreq << "    f2 = " << 5.0 * natFreq << endln;
		opserr << "a0 = " << a0 << "    a1 = " << a1 << endln;
	}
	theDomain->setRayleighDampingFactors(a0, a1, 0.0, 0.0);

	OPS_Stream* theOutputStream;
	OPS_Stream* theOutputStream2;
	Recorder* theRecorder;
	ID nodesToRecord(8);
	ID dofToRecord(3);
	ID elemsToRecord(5);
	nodesToRecord[0] = 1;
	nodesToRecord[1] = 2;
	nodesToRecord[2] = 3;
	nodesToRecord[3] = 4;
	nodesToRecord[4] = 5;
	nodesToRecord[5] = 6;
	nodesToRecord[6] = 7;
	nodesToRecord[7] = 8;
	dofToRecord[0] = 0;
	dofToRecord[1] = 1;
	dofToRecord[2] = 2;
	elemsToRecord[0] = 1;
	elemsToRecord[1] = 2;
	elemsToRecord[2] = 3;
	elemsToRecord[3] = 4;
	elemsToRecord[4] = 5;
	const char* eleArgs = "stress";

	theOutputStream = new DataFileStream("Output.out", OVERWRITE, 2, 0, false, 6, false);
	theOutputStream2 = new DataFileStream("Output2.out", OVERWRITE, 2, 0, false, 6, false);
	theRecorder = new NodeRecorder(dofToRecord, &nodesToRecord, 0, "accel", *theDomain, *theOutputStream, 0.0, true, NULL);
	theDomain->addRecorder(*theRecorder);

	theRecorder = new ElementRecorder(&elemsToRecord, &eleArgs, 1, true, *theDomain, *theOutputStream2, 0.0, NULL);
	theDomain->addRecorder(*theRecorder);

	for (int analysisCount = 0; analysisCount < 8000; ++analysisCount) {
		//int converged = theAnalysis->analyze(1, 0.01, 0.005, 0.02, 1);
		int converged = theTransientAnalysis->analyze(1, 0.002, 0.001, 0.02, 1);
		//int converged = theTransientAnalysis->analyze(1, 0.002);
		if (!converged) {
			opserr << "Converged at time " << theDomain->getCurrentTime() << endln;
		}
	}

	if (PRINTDEBUG)
	{
		Information info;
		theEle = theDomain->getElement(1);
		theEle->getResponse(1, info);
		opserr << "Stress = " << info.getData();
		theEle->getResponse(2, info);
		opserr << "Strain = " << info.getData();
	}

	return 0;
}



int
SiteResponseModel::generateTestModel()
{
	Vector zeroVec(3);
	zeroVec.Zero();

	Node* theNode;

	theNode = new Node(1, 3, 0.0, 0.0, 0.0); theDomain->addNode(theNode);
	theNode = new Node(2, 3, 1.0, 0.0, 0.0); theDomain->addNode(theNode);
	theNode = new Node(3, 3, 1.0, 1.0, 0.0); theDomain->addNode(theNode);
	theNode = new Node(4, 3, 0.0, 1.0, 0.0); theDomain->addNode(theNode);
	theNode = new Node(5, 3, 0.0, 0.0, 1.0); theDomain->addNode(theNode);
	theNode = new Node(6, 3, 1.0, 0.0, 1.0); theDomain->addNode(theNode);
	theNode = new Node(7, 3, 1.0, 1.0, 1.0); theDomain->addNode(theNode);
	theNode = new Node(8, 3, 0.0, 1.0, 1.0); theDomain->addNode(theNode);

	SP_Constraint* theSP;
	for (int counter = 0; counter < 3; ++counter) {
		theSP = new SP_Constraint(1, counter, 0.0, true); theDomain->addSP_Constraint(theSP);
		theSP = new SP_Constraint(2, counter, 0.0, true); theDomain->addSP_Constraint(theSP);
		theSP = new SP_Constraint(3, counter, 0.0, true); theDomain->addSP_Constraint(theSP);
		theSP = new SP_Constraint(4, counter, 0.0, true); theDomain->addSP_Constraint(theSP);
	}

	MP_Constraint* theMP;
	Matrix Ccr(3, 3); Ccr(0, 0) = 1.0; Ccr(1, 1) = 1.0; Ccr(2, 2) = 1.0;
	ID rcDOF(3); rcDOF(0) = 0; rcDOF(1) = 1; rcDOF(2) = 2;
	theMP = new MP_Constraint(5, 6, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);
	theMP = new MP_Constraint(5, 7, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);
	theMP = new MP_Constraint(5, 8, Ccr, rcDOF, rcDOF); theDomain->addMP_Constraint(theMP);

	NDMaterial* theMat;
	theMat = new J2CyclicBoundingSurface(1, 20000.0, 25000.0, 100.0, 0.0, 20000.0, 1.0, 0.0, 0.5);
	OPS_addNDMaterial(theMat);

	Element* theEle;
	theMat = OPS_getNDMaterial(1);
	//theEle = new SSPbrick(1, 1, 2, 3, 4, 5, 6, 7, 8, *theMat, 0.0, 0.0, 0.0); theDomain->addElement(theEle);
	theEle = new Brick(1, 1, 2, 3, 4, 5, 6, 7, 8, *theMat, 0.0, 0.0, 0.0); theDomain->addElement(theEle);

	//LinearSeries* theTS_disp;
	//theTS_disp = new LinearSeries(1, 1.0);

	Vector theTime(3);
	theTime(0) = 0.0;
	theTime(1) = 1.0;
	theTime(2) = 100.0;

	Vector theValue_Disp(3);
	theValue_Disp(0) = 0.0;
	theValue_Disp(1) = 1.0;
	theValue_Disp(2) = 1.0;

	Vector theValue_Vel(3);
	theValue_Vel(0) = 1.0;
	theValue_Vel(1) = 1.0;
	theValue_Vel(2) = 1.0;

	Vector theValue_Acc(3);
	theValue_Acc(0) = 0.0;
	theValue_Acc(1) = 0.0;
	theValue_Acc(2) = 0.0;
	PathTimeSeries* theTS_disp = new PathTimeSeries(1, theValue_Disp, theTime, 1.0, true);
	//PathTimeSeries* theTS_disp = NULL;
	PathTimeSeries* theTS_vel = new PathTimeSeries(1, theValue_Vel, theTime, 1.0, true);
	//PathTimeSeries* theTS_vel  = NULL;
	PathTimeSeries* theTS_acc = new PathTimeSeries(1, theValue_Acc, theTime, 1.0, true);
	//PathTimeSeries* theTS_acc = NULL;

	//LoadPattern* theLP;
	//theLP = new LoadPattern(1);
	//theLP->setTimeSeries(theTS_disp);

	MultiSupportPattern* theLP = new MultiSupportPattern(1);
	//theLP->setTimeSeries(theTS_disp);

	//NodalLoad* theLoad;
	//Vector load(3);
	//load(0) = 1.0;
	//load(1) = 0.0;
	//load(2) = 0.0;
	//theLoad = new NodalLoad(1, 5, load, false); theLP->addNodalLoad(theLoad);
	//theLoad = new NodalLoad(2, 6, load, false); theLP->addNodalLoad(theLoad);
	//theLoad = new NodalLoad(3, 7, load, false); theLP->addNodalLoad(theLoad);
	//theLoad = new NodalLoad(4, 8, load, false); theLP->addNodalLoad(theLoad);
	GroundMotion* theMotion = new GroundMotion(theTS_disp, theTS_vel, theTS_acc);
	theLP->addMotion(*theMotion, 1);

	theLP->addSP_Constraint(new ImposedMotionSP(5, 0, 1, 1));
	theLP->addSP_Constraint(new ImposedMotionSP(5, 0, 1, 1));
	theLP->addSP_Constraint(new ImposedMotionSP(5, 0, 1, 1));
	theLP->addSP_Constraint(new ImposedMotionSP(5, 0, 1, 1));

	//theLP->addSP_Constraint(new SP_Constraint(5, 0, 1.0, false));
	//theLP->addSP_Constraint(new SP_Constraint(6, 0, 1.0, false));
	//theLP->addSP_Constraint(new SP_Constraint(7, 0, 1.0, false));
	//theLP->addSP_Constraint(new SP_Constraint(8, 0, 1.0, false));
	theDomain->addLoadPattern(theLP);

	AnalysisModel* theModel = new AnalysisModel();
	CTestNormDispIncr* theTest = new CTestNormDispIncr(1.0e-7, 30, 1);
	EquiSolnAlgo* theSolnAlgo = new NewtonRaphson(*theTest);
	//StaticIntegrator* theIntegrator    = new LoadControl(0.05, 1, 0.05, 1.0);
	TransientIntegrator* theIntegrator = new Newmark(0.5, 0.25);
	ConstraintHandler* theHandler = new PenaltyConstraintHandler(1.0e15, 1.0e15);
	RCM* theRCM = new RCM();
	DOF_Numberer* theNumberer = new DOF_Numberer(*theRCM);
	BandGenLinSolver* theSolver = new BandGenLinLapackSolver();
	LinearSOE* theSOE = new BandGenLinSOE(*theSolver);



	//DirectIntegrationAnalysis* theAnalysis;
	//theAnalysis = new DirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator, theTest);

	VariableTimeStepDirectIntegrationAnalysis* theAnalysis;
	theAnalysis = new VariableTimeStepDirectIntegrationAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator, theTest);

	//StaticAnalysis *theAnalysis;
	//theAnalysis = new StaticAnalysis(*theDomain, *theHandler, *theNumberer, *theModel, *theSolnAlgo, *theSOE, *theIntegrator);
	//theAnalysis->setConvergenceTest(*theTest);

	for (int analysisCount = 0; analysisCount < 15; ++analysisCount) {
		int converged = theAnalysis->analyze(1, 0.01, 0.005, 0.02, 1);
		if (!converged) {
			opserr << "Converged at time " << theDomain->getCurrentTime() << endln;

			opserr << "Disp = " << theDomain->getNode(5)->getDisp()(0);
			opserr << ", Vel = " << theDomain->getNode(5)->getTrialVel()(0);
			opserr << ", acc = " << theDomain->getNode(5)->getTrialAccel()(0) << endln;

			opserr << "From the ground motion: " << endln;
			opserr << "Disp = " << theMotion->getDisp(theDomain->getCurrentTime());
			opserr << ", Vel = " << theMotion->getVel(theDomain->getCurrentTime());
			opserr << ", acc = " << theMotion->getAccel(theDomain->getCurrentTime()) << endln;
		}
	}

	Information info;
	theEle->getResponse(1, info);
	opserr << "Stress = " << info.getData();
	theEle->getResponse(2, info);
	opserr << "Strain = " << info.getData();



	return 0;
}