//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "STLFilter.h"

//Qt
#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringList>
#include <QString>
#include <QMessageBox>
#include <QPushButton>

//qCC_db
#include <ccLog.h>
#include <ccMesh.h>
#include <ccPointCloud.h>
#include <ccProgressDialog.h>
#include <ccNormalVectors.h>
#include <ccOctree.h>

//System
#include <string.h>

bool STLFilter::canLoadExtension(QString upperCaseExt) const
{
	return (upperCaseExt == "STL");
}

bool STLFilter::canSave(CC_CLASS_ENUM type, bool& multiple, bool& exclusive) const
{
	if (type == CC_TYPES::MESH)
	{
		multiple = false;
		exclusive = true;
		return true;
	}
	return false;
}

CC_FILE_ERROR STLFilter::saveToFile(ccHObject* entity, QString filename, SaveParameters& parameters)
{
	if (!entity)
		return CC_FERR_BAD_ARGUMENT;

	if (!entity->isKindOf(CC_TYPES::MESH))
		return CC_FERR_BAD_ENTITY_TYPE;

	ccGenericMesh* mesh = ccHObjectCaster::ToGenericMesh(entity);
	if (!mesh || mesh->size() == 0)
	{
		ccLog::Warning(QString("[STL] No facet in mesh '%1'!").arg(mesh->getName()));
		return CC_FERR_NO_ERROR;
	}

	//ask for output format
	QMessageBox msgBox(QMessageBox::Question,"Choose output format","Save in BINARY or ASCII format?");
	QPushButton *binaryButton = msgBox.addButton("BINARY", QMessageBox::AcceptRole);
	msgBox.addButton("ASCII", QMessageBox::AcceptRole);
	msgBox.exec();

	//try to open file for saving
	FILE* theFile = fopen(qPrintable(filename),"wb");
	if (!theFile)
		return CC_FERR_WRITING;

	CC_FILE_ERROR result = CC_FERR_NO_ERROR;
	if (msgBox.clickedButton() == binaryButton)
	{
		result = saveToBINFile(mesh, theFile);
	}
	else //if (msgBox.clickedButton() == asciiButton)
	{
		result = saveToASCIIFile(mesh, theFile);
	}

	fclose(theFile);

	return result;
}

CC_FILE_ERROR STLFilter::saveToBINFile(ccGenericMesh* mesh, FILE *theFile)
{
	assert(theFile && mesh && mesh->size()!=0);
	unsigned faceCount = mesh->size();
	
	//progress
	ccProgressDialog pDlg(true);
	CCLib::NormalizedProgress nprogress(&pDlg,faceCount);
	pDlg.setMethodTitle(qPrintable(QString("Saving mesh [%1]").arg(mesh->getName())));
	pDlg.setInfo(qPrintable(QString("Number of facets: %1").arg(faceCount)));
	pDlg.start();
	QApplication::processEvents();

	//header
	{
		char header[80];
		memset(header,0,80);
		strcpy(header,"Binary STL file generated by CloudCompare!");
		if (fwrite(header,80,1,theFile) < 1)
			return CC_FERR_WRITING;
	}

	//UINT32 Number of triangles
	{
		uint32_t tmpInt32 = static_cast<uint32_t>(faceCount);
		if (fwrite((const void*)&tmpInt32,4,1,theFile) < 1)
			return CC_FERR_WRITING;
	}

	ccGenericPointCloud* vertices = mesh->getAssociatedCloud();
	assert(vertices);

	//Can't save global shift information....
	if (vertices->isShifted())
	{
		ccLog::Warning("[STL] Global shift information can't be restored in STL Binary format! (too low precision)");
	}

	mesh->placeIteratorAtBegining();
	for (unsigned i=0; i<faceCount; ++i)
	{
		CCLib::VerticesIndexes*tsi = mesh->getNextTriangleVertIndexes();

		const CCVector3* A = vertices->getPointPersistentPtr(tsi->i1);
		const CCVector3* B = vertices->getPointPersistentPtr(tsi->i2);
		const CCVector3* C = vertices->getPointPersistentPtr(tsi->i3);
		//compute face normal (right hand rule)
		CCVector3 N = (*B-*A).cross(*C-*A);

		//REAL32[3] Normal vector
		Vector3Tpl<float> buffer =  Vector3Tpl<float>::fromArray(N.u); //convert to an explicit float array (as PointCoordinateType may be a double!)
		assert(sizeof(float) == 4);
		if (fwrite((const void*)buffer.u,4,3,theFile) < 3)
			return CC_FERR_WRITING;

		//REAL32[3] Vertex 1,2 & 3
		buffer =  Vector3Tpl<float>::fromArray(A->u); //convert to an explicit float array (as PointCoordinateType may be a double!)
		if (fwrite((const void*)buffer.u,4,3,theFile) < 3)
			return CC_FERR_WRITING;
		buffer =  Vector3Tpl<float>::fromArray(B->u); //convert to an explicit float array (as PointCoordinateType may be a double!)
		if (fwrite((const void*)buffer.u,4,3,theFile) < 3)
			return CC_FERR_WRITING;
		buffer =  Vector3Tpl<float>::fromArray(C->u); //convert to an explicit float array (as PointCoordinateType may be a double!)
		if (fwrite((const void*)buffer.u,4,3,theFile) < 3)
			return CC_FERR_WRITING;

		//UINT16 Attribute byte count (not used)
		{
			char byteCount[2] = {0,0};
			if (fwrite(byteCount,2,1,theFile) < 1)
				return CC_FERR_WRITING;
		}

		//progress
		if (!nprogress.oneStep())
			return CC_FERR_CANCELED_BY_USER;
	}

	pDlg.stop();

	return CC_FERR_NO_ERROR;
}

CC_FILE_ERROR STLFilter::saveToASCIIFile(ccGenericMesh* mesh, FILE *theFile)
{
	assert(theFile && mesh && mesh->size()!=0);
	unsigned faceCount = mesh->size();
	
	//progress
	ccProgressDialog pDlg(true);
	CCLib::NormalizedProgress nprogress(&pDlg,faceCount);
	pDlg.setMethodTitle(qPrintable(QString("Saving mesh [%1]").arg(mesh->getName())));
	pDlg.setInfo(qPrintable(QString("Number of facets: %1").arg(faceCount)));
	pDlg.start();
	QApplication::processEvents();

	if (fprintf(theFile,"solid %s\n",qPrintable(mesh->getName())) < 0) //empty names are acceptable!
		return CC_FERR_WRITING;

	//vertices
	ccGenericPointCloud* vertices = mesh->getAssociatedCloud();

	mesh->placeIteratorAtBegining();
	for (unsigned i=0; i<faceCount; ++i)
	{
		CCLib::VerticesIndexes*tsi = mesh->getNextTriangleVertIndexes();

		const CCVector3* A = vertices->getPointPersistentPtr(tsi->i1);
		const CCVector3* B = vertices->getPointPersistentPtr(tsi->i2);
		const CCVector3* C = vertices->getPointPersistentPtr(tsi->i3);
		//compute face normal (right hand rule)
		CCVector3 N = (*B-*A).cross(*C-*A);

		//%e = scientific notation
		if (fprintf(theFile,"facet normal %e %e %e\n",N.x,N.y,N.z) < 0)
			return CC_FERR_WRITING;
		if (fprintf(theFile,"outer loop\n") < 0)
			return CC_FERR_WRITING;

		CCVector3d Aglobal = vertices->toGlobal3d<PointCoordinateType>(*A);
		if (fprintf(theFile,"vertex %e %e %e\n",Aglobal.x,
												Aglobal.y,
												Aglobal.z) < 0)
			return CC_FERR_WRITING;
		CCVector3d Bglobal = vertices->toGlobal3d<PointCoordinateType>(*B);
		if (fprintf(theFile,"vertex %e %e %e\n",Bglobal.x,
												Bglobal.y,
												Bglobal.z) < 0)
			return CC_FERR_WRITING;
		CCVector3d Cglobal = vertices->toGlobal3d<PointCoordinateType>(*C);
		if (fprintf(theFile,"vertex %e %e %e\n",Cglobal.x,
												Cglobal.y,
												Cglobal.z) < 0)
			return CC_FERR_WRITING;
		if (fprintf(theFile,"endloop\nendfacet\n") < 0)
			return CC_FERR_WRITING;

		//progress
		if (!nprogress.oneStep())
			return CC_FERR_CANCELED_BY_USER;
	}

	if (fprintf(theFile,"endsolid %s\n",qPrintable(mesh->getName())) < 0) //empty names are acceptable!
		return CC_FERR_WRITING;

	return CC_FERR_NO_ERROR;
}

const PointCoordinateType c_defaultSearchRadius = static_cast<PointCoordinateType>(sqrt(ZERO_TOLERANCE));
static bool TagDuplicatedVertices(	const CCLib::DgmOctree::octreeCell& cell,
									void** additionalParameters,
									CCLib::NormalizedProgress* nProgress/*=0*/)
{
	GenericChunkedArray<1,int>* equivalentIndexes = static_cast<GenericChunkedArray<1,int>*>(additionalParameters[0]);

	//we look for points very near to the others (only if not yet tagged!)
	
	//structure for nearest neighbors search
	CCLib::DgmOctree::NearestNeighboursSphericalSearchStruct nNSS;
	nNSS.level = cell.level;
	nNSS.prepare(c_defaultSearchRadius,cell.parentOctree->getCellSize(nNSS.level));
	cell.parentOctree->getCellPos(cell.truncatedCode,cell.level,nNSS.cellPos,true);
	cell.parentOctree->computeCellCenter(nNSS.cellPos,cell.level,nNSS.cellCenter);
	//*/

	unsigned n = cell.points->size(); //number of points in the current cell
	
	//we already know some of the neighbours: the points in the current cell!
	try
	{
		nNSS.pointsInNeighbourhood.resize(n);
	}
	catch (.../*const std::bad_alloc&*/) //out of memory
	{
		return false;
	}

	//init structure with cell points
	{
		CCLib::DgmOctree::NeighboursSet::iterator it = nNSS.pointsInNeighbourhood.begin();
		for (unsigned i=0; i<n; ++i,++it)
		{
			it->point = cell.points->getPointPersistentPtr(i);
			it->pointIndex = cell.points->getPointGlobalIndex(i);
		}
		nNSS.alreadyVisitedNeighbourhoodSize = 1;
	}

	//for each point in the cell
	for (unsigned i=0; i<n; ++i)
	{
		int thisIndex = static_cast<int>(cell.points->getPointGlobalIndex(i));
		if (equivalentIndexes->getValue(thisIndex) < 0) //has no equivalent yet 
		{
			cell.points->getPoint(i,nNSS.queryPoint);

			//look for neighbors in a (very small) sphere
			//warning: there may be more points at the end of nNSS.pointsInNeighbourhood than the actual nearest neighbors (k)!
			unsigned k = cell.parentOctree->findNeighborsInASphereStartingFromCell(nNSS,c_defaultSearchRadius,false);

			//if there are some very close points
			if (k>1)
			{
				for (unsigned j=0; j<k; ++j)
				{
					//all the other points are equivalent to the query point
					const unsigned& otherIndex = nNSS.pointsInNeighbourhood[j].pointIndex;
					if (static_cast<int>(otherIndex) != thisIndex)
						equivalentIndexes->setValue(otherIndex,thisIndex);
				}
			}

			//and the query point is always root
			equivalentIndexes->setValue(thisIndex,thisIndex);
		}

		if (nProgress && !nProgress->oneStep())
			return false;
	}

	return true;
}

CC_FILE_ERROR STLFilter::loadFile(QString filename, ccHObject& container, LoadParameters& parameters)
{
	ccLog::Print(QString("[STL] Loading '%1'").arg(filename));

	//try to open the file
	QFile fp(filename);
	if (!fp.open(QIODevice::ReadOnly))
		return CC_FERR_READING;

	//ASCII OR BINARY?
	QString name("mesh");
	bool ascii = true;
	{
		//buffer
		char header[80] = { 0 };
		qint64 sz = fp.read(header,80);
		if (sz<80)
		{
			//either ASCII or BINARY STL FILES are always > 80 bytes
			return sz == 0 ? CC_FERR_READING : CC_FERR_MALFORMED_FILE;
		}
		//normally, binary files shouldn't start by 'solid'
		if (!QString(header).trimmed().toUpper().startsWith("SOLID"))
		{
			ascii = false;
		}
		else //... but sadly some BINARY files does start by SOLID?!!!! (wtf)
		{
			//go back to the beginning of the file
			fp.seek(0);
			
			QTextStream stream(&fp);
			//skip first line
			stream.readLine();
			//we look if the second line (if any) starts by 'facet'
			QString line = stream.readLine();
			ascii = true;
			if (	line.isEmpty()
				||	fp.error() != QFile::NoError
				||	!QString(line).trimmed().toUpper().startsWith("FACET"))
			{
				ascii = false;
			}
		}
		//go back to the beginning of the file
		fp.seek(0);
	}
	ccLog::Print("[STL] Detected format: %s",ascii ? "ASCII" : "BINARY");

	//vertices
	ccPointCloud* vertices = new ccPointCloud("vertices");
	//mesh
	ccMesh* mesh = new ccMesh(vertices);
	mesh->setName(name);
	//add normals
	mesh->setTriNormsTable(new NormsIndexesTableType());

	CC_FILE_ERROR error = CC_FERR_NO_ERROR;
	if (ascii)
		error = loadASCIIFile(fp,mesh,vertices,parameters);
	else
		error = loadBinaryFile(fp,mesh,vertices,parameters);

	if (error != CC_FERR_NO_ERROR)
	{
		return CC_FERR_MALFORMED_FILE;
	}

	unsigned vertCount = vertices->size();
	unsigned faceCount = mesh->size();
	ccLog::Print("[STL] %i points, %i face(s)",vertCount,faceCount);

	//do some cleaning
	{
		vertices->shrinkToFit();
		mesh->shrinkToFit();
		NormsIndexesTableType* normals = mesh->getTriNormsTable();
		if (normals)
			normals->shrinkToFit();
	}

	//remove duplicated vertices
	//if (false)
	{
		GenericChunkedArray<1,int>* equivalentIndexes = new GenericChunkedArray<1,int>;
		const int razValue = -1;
		if (equivalentIndexes && equivalentIndexes->resize(vertCount,true,razValue))
		{
			ccProgressDialog pDlg(true);
			ccOctree* octree = vertices->computeOctree(&pDlg);
			if (octree)
			{
				void* additionalParameters[] = { static_cast<void*>(equivalentIndexes) };
				unsigned result = octree->executeFunctionForAllCellsAtLevel(10,
																			TagDuplicatedVertices,
																			additionalParameters,
																			false,
																			&pDlg,
																			"Tag duplicated vertices");
				vertices->deleteOctree();
				octree = 0;

				if (result != 0)
				{
					unsigned remainingCount = 0;
					for (unsigned i=0; i<vertCount; ++i)
					{
						int eqIndex = equivalentIndexes->getValue(i);
						assert(eqIndex >= 0);
						if (eqIndex == static_cast<int>(i)) //root point
						{
							int newIndex = static_cast<int>(vertCount+remainingCount); //We replace the root index by its 'new' index (+ vertCount, to differentiate it later)
							equivalentIndexes->setValue(i,newIndex);
							++remainingCount;
						}
					}

					ccPointCloud* newVertices = new ccPointCloud("vertices");
					if (newVertices->reserve(remainingCount))
					{
						//copy root points in a new cloud
						{
							for (unsigned i=0; i<vertCount; ++i)
							{
								int eqIndex = equivalentIndexes->getValue(i);
								if (eqIndex >= static_cast<int>(vertCount)) //root point
									newVertices->addPoint(*vertices->getPoint(i));
								else
									equivalentIndexes->setValue(i,equivalentIndexes->getValue(eqIndex)); //and update the other indexes
							}
						}

						//update face indexes
						{
							unsigned newFaceCount = 0;
							for (unsigned i=0; i<faceCount; ++i)
							{
								CCLib::VerticesIndexes* tri = mesh->getTriangleVertIndexes(i);
								tri->i1 = static_cast<unsigned>(equivalentIndexes->getValue(tri->i1))-vertCount;
								tri->i2 = static_cast<unsigned>(equivalentIndexes->getValue(tri->i2))-vertCount;
								tri->i3 = static_cast<unsigned>(equivalentIndexes->getValue(tri->i3))-vertCount;

								//very small triangles (or flat ones) may be implicitly removed by vertex fusion!
								if (tri->i1 != tri->i2 && tri->i1 != tri->i3 && tri->i2 != tri->i3)
								{
									if (newFaceCount != i)
										mesh->swapTriangles(i,newFaceCount);
									++newFaceCount;
								}
							}

							if (newFaceCount == 0)
							{
								ccLog::Warning("[STL] After vertex fusion, all triangles would collapse! We'll keep the non-fused version...");
								delete newVertices;
								newVertices = 0;
							}
							else
							{
								mesh->resize(newFaceCount);
							}
						}
						
						if (newVertices)
						{
							mesh->setAssociatedCloud(newVertices);
							delete vertices;
							vertices = newVertices;
							vertCount = vertices->size();
							ccLog::Print("[STL] Remaining vertices after auto-removal of duplicate ones: %i",vertCount);
							ccLog::Print("[STL] Remaining faces after auto-removal of duplicate ones: %i",mesh->size());
						}
					}
					else
					{
						ccLog::Warning("[STL] Not enough memory: couldn't removed duplicated vertices!");
					}
				}
				else
				{
					ccLog::Warning("[STL] Duplicated vertices removal algorithm failed?!");
				}
			}
			else
			{
				ccLog::Warning("[STL] Not enough memory: couldn't removed duplicated vertices!");
			}
		}
		else
		{
			ccLog::Warning("[STL] Not enough memory: couldn't removed duplicated vertices!");
		}

		if (equivalentIndexes)
			equivalentIndexes->release();
		equivalentIndexes = 0;
	}

	NormsIndexesTableType* normals = mesh->getTriNormsTable();
	if (normals)
	{
		//normals->link();
		//mesh->addChild(normals); //automatically done by setTriNormsTable
		mesh->showNormals(true);
	}
	else
	{
		//DGM: normals can be per-vertex or per-triangle so it's better to let the user do it himself later
		//Moreover it's not always good idea if the user doesn't want normals (especially in ccViewer!)
		//if (mesh->computeNormals())
		//	mesh->showNormals(true);
		//else
		//	ccLog::Warning("[STL] Failed to compute per-vertex normals...");
		ccLog::Warning("[STL] Mesh has no normal! You can manually compute them (select it then call \"Edit > Normals > Compute\")");
	}
	vertices->setEnabled(false);
	vertices->setLocked(false); //DGM: no need to lock it as it is only used by one mesh!
	mesh->addChild(vertices);

	container.addChild(mesh);

	return CC_FERR_NO_ERROR;
}

CC_FILE_ERROR STLFilter::loadASCIIFile(	QFile& fp,
										ccMesh* mesh,
										ccPointCloud* vertices,
										LoadParameters& parameters)
{
	assert(fp.isOpen() && mesh && vertices);

	//text stream
	QTextStream stream(&fp);

	//1st line: 'solid name'
	QString name("mesh");
	{
		QString currentLine = stream.readLine();
		if (currentLine.isEmpty() || fp.error() != QFile::NoError)
		{
			return CC_FERR_READING;
		}
		QStringList tokens = currentLine.split(QRegExp("\\s+"),QString::SkipEmptyParts);
		if (tokens.empty() || tokens[0].toUpper() != "SOLID")
		{
			ccLog::Warning("[STL] File should begin by 'solid [name]'!");
			return CC_FERR_MALFORMED_FILE;
		}
		//Extract name
		if (tokens.size()>1)
		{
			tokens.removeAt(0);
			name = tokens.join(" ");
		}
	}
	mesh->setName(name);

	//progress dialog
	ccProgressDialog pDlg(true);
	pDlg.setMethodTitle("(ASCII) STL file");
	pDlg.setInfo("Loading in progress...");
	pDlg.setRange(0,0);
	pDlg.show();
	QApplication::processEvents();

	//current vertex shift
	CCVector3d Pshift(0,0,0);

	unsigned pointCount = 0;
	unsigned faceCount = 0;
	bool normalWarningAlreadyDisplayed = false;
	NormsIndexesTableType* normals = mesh->getTriNormsTable();

	CC_FILE_ERROR result = CC_FERR_NO_ERROR;

	unsigned lineCount = 1;
	while (true)
	{
		CCVector3 N;
		bool normalIsOk = false;

		//1st line of a 'facet': "facet normal ni nj nk" / or 'endsolid' (i.e. end of file)
		{
			QString currentLine = stream.readLine();
			if (currentLine.isEmpty())
			{
				break;
			}
			else if (fp.error() != QFile::NoError)
			{
				result = CC_FERR_READING;
				break;
			}
			++lineCount;

			QStringList tokens = currentLine.split(QRegExp("\\s+"),QString::SkipEmptyParts);
			if (tokens.empty() || tokens[0].toUpper() != "FACET")
			{
				if (tokens[0].toUpper() != "ENDSOLID")
				{
					ccLog::Warning("[STL] Error on line #%i: line should start by 'facet'!",lineCount);
					return CC_FERR_MALFORMED_FILE;
				}
				break;
			}

			if (normals && tokens.size() >= 5)
			{
				//let's try to read normal
				if (tokens[1].toUpper() == "NORMAL")
				{
					N.x = static_cast<PointCoordinateType>(tokens[2].toDouble(&normalIsOk));
					if (normalIsOk)
					{
						N.y = static_cast<PointCoordinateType>(tokens[3].toDouble(&normalIsOk));
						if (normalIsOk)
						{
							N.z = static_cast<PointCoordinateType>(tokens[4].toDouble(&normalIsOk));
						}
					}
					if (!normalIsOk && !normalWarningAlreadyDisplayed)
					{
						ccLog::Warning("[STL] Error on line #%i: failed to read 'normal' values!",lineCount);
						normalWarningAlreadyDisplayed = true;
					}
				}
				else if (!normalWarningAlreadyDisplayed)
				{
					ccLog::Warning("[STL] Error on line #%i: expecting 'normal' after 'facet'!",lineCount);
					normalWarningAlreadyDisplayed = true;
				}
			}
			else if (tokens.size() > 1 && !normalWarningAlreadyDisplayed)
			{
				ccLog::Warning("[STL] Error on line #%i: incomplete 'normal' description!",lineCount);
				normalWarningAlreadyDisplayed = true;
			}
		}

		//2nd line: 'outer loop'
		{
			QString currentLine = stream.readLine();
			if (	currentLine.isEmpty()
				||	fp.error() != QFile::NoError
				|| !QString(currentLine).trimmed().toUpper().startsWith("OUTER LOOP"))
			{
				ccLog::Warning("[STL] Error: expecting 'outer loop' on line #%i",lineCount+1);
				result = CC_FERR_READING;
				break;
			}
			++lineCount;
		}

		//3rd to 5th lines: 'vertex vix viy viz'
		unsigned vertIndexes[3];
//		unsigned pointCountBefore = pointCount;
		for (unsigned i=0; i<3; ++i)
		{
			QString currentLine = stream.readLine();
			if (	currentLine.isEmpty()
				||	fp.error() != QFile::NoError
				|| !QString(currentLine).trimmed().toUpper().startsWith("VERTEX"))
			{
				ccLog::Warning("[STL] Error: expecting a line starting by 'vertex' on line #%i",lineCount+1);
				result = CC_FERR_MALFORMED_FILE;
				break;
			}
			++lineCount;

			QStringList tokens = QString(currentLine).split(QRegExp("\\s+"),QString::SkipEmptyParts);
			if (tokens.size() < 4)
			{
				ccLog::Warning("[STL] Error on line #%i: incomplete 'vertex' description!",lineCount);
				result = CC_FERR_MALFORMED_FILE;
				break;
			}

			//read vertex
			CCVector3d Pd(0,0,0);
			{
				bool vertexIsOk = false;
				Pd.x = tokens[1].toDouble(&vertexIsOk);
				if (vertexIsOk)
				{
					Pd.y = tokens[2].toDouble(&vertexIsOk);
					if (vertexIsOk)
						Pd.z = tokens[3].toDouble(&vertexIsOk);
				}
				if (!vertexIsOk)
				{
					ccLog::Warning("[STL] Error on line #%i: failed to read 'vertex' coordinates!",lineCount);
					result = CC_FERR_MALFORMED_FILE;
					break;
				}
			}

			//first point: check for 'big' coordinates
			if (pointCount == 0)
			{
				if (HandleGlobalShift(Pd,Pshift,parameters))
				{
					vertices->setGlobalShift(Pshift);
					ccLog::Warning("[STLFilter::loadFile] Cloud has been recentered! Translation: (%.2f,%.2f,%.2f)",Pshift.x,Pshift.y,Pshift.z);
				}
			}

			CCVector3 P = CCVector3::fromArray((Pd + Pshift).u);

			//look for existing vertices at the same place! (STL format is so dumb...)
			{
				//int equivalentIndex = -1;
				//if (pointCount>2)
				//{
				//	//brute force!
				//	for (int j=(int)pointCountBefore-1; j>=0; j--)
				//	{
				//		const CCVector3* Pj = vertices->getPoint(j);
				//		if (Pj->x == P.x &&
				//			Pj->y == P.y &&
				//			Pj->z == P.z)
				//		{
				//			equivalentIndex = j;
				//			break;
				//		}
				//	}
				//}

				////new point ?
				//if (equivalentIndex < 0)
				{
					//cloud is already full?
					if (vertices->capacity() == pointCount && !vertices->reserve(pointCount+1000))
						return CC_FERR_NOT_ENOUGH_MEMORY;

					//insert new point
					vertIndexes[i] = pointCount++;
					vertices->addPoint(P);
				}
				//else
				//{
				//	vertIndexes[i] = (unsigned)equivalentIndex;
				//}
			}
		}

		//we have successfully read the 3 vertices
		//let's add a new triangle
		{
			//mesh is full?
			if (mesh->capacity() == faceCount)
			{
				if (!mesh->reserve(faceCount+1000))
				{
					result = CC_FERR_NOT_ENOUGH_MEMORY;
					break;
				}

				if (normals)
				{
					bool success = normals->reserve(mesh->capacity());
					if (success && faceCount == 0) //specific case: allocate per triangle normal indexes the first time!
						success = mesh->reservePerTriangleNormalIndexes();

					if (!success)
					{
						ccLog::Warning("[STL] Not enough memory: can't store normals!");
						mesh->removePerTriangleNormalIndexes();
						mesh->setTriNormsTable(0);
						normals = 0;
					}
				}
			}

			mesh->addTriangle(vertIndexes[0],vertIndexes[1],vertIndexes[2]);
			++faceCount;
		}

		//and a new normal?
		if (normals)
		{
			int index = -1;
			if (normalIsOk)
			{
				//compress normal
				index = static_cast<int>(normals->currentSize());
				CompressedNormType nIndex = ccNormalVectors::GetNormIndex(N.u);
				normals->addElement(nIndex);
			}
			mesh->addTriangleNormalIndexes(index,index,index);
		}

		//6th line: 'endloop'
		{
			QString currentLine = stream.readLine();
			if (	currentLine.isEmpty()
				||	fp.error() != QFile::NoError
				|| !QString(currentLine).trimmed().toUpper().startsWith("ENDLOOP"))
			{
				ccLog::Warning("[STL] Error: expecting 'endnloop' on line #%i",lineCount+1);
				result = CC_FERR_MALFORMED_FILE;
				break;
			}
			++lineCount;
		}

		//7th and last line: 'endfacet'
		{
			QString currentLine = stream.readLine();
			if (	currentLine.isEmpty()
				||	fp.error() != QFile::NoError
				|| !QString(currentLine).trimmed().toUpper().startsWith("ENDFACET"))
			{
				ccLog::Warning("[STL] Error: expecting 'endfacet' on line #%i",lineCount+1);
				result = CC_FERR_MALFORMED_FILE;
				break;
			}
			++lineCount;
		}

		//progress
		if ((faceCount % 1024) == 0)
		{
			if (pDlg.wasCanceled())
				break;
			pDlg.setValue(static_cast<int>(faceCount>>10));
		}
	}

	if (normalWarningAlreadyDisplayed)
	{
		ccLog::Warning("[STL] Failed to read some 'normal' values!");
	}

	pDlg.close();

	return result;
}

CC_FILE_ERROR STLFilter::loadBinaryFile(QFile& fp,
										ccMesh* mesh,
										ccPointCloud* vertices,
										LoadParameters& parameters)
{
	assert(fp.isOpen() && mesh && vertices);

	unsigned pointCount = 0;
	unsigned faceCount = 0;

	//UINT8[80] Header (we skip it)
	fp.seek(80);
	mesh->setName("Mesh"); //hard to guess solid name with binary files!

	//UINT32 Number of triangles
	{
		unsigned tmpInt32;
		if (fp.read((char*)&tmpInt32,4) < 4)
			return CC_FERR_READING;
		faceCount = tmpInt32;
	}

	if (!mesh->reserve(faceCount))
		return CC_FERR_NOT_ENOUGH_MEMORY;
	NormsIndexesTableType* normals = mesh->getTriNormsTable();
	if (normals && (!normals->reserve(faceCount) || !mesh->reservePerTriangleNormalIndexes()))
	{
		ccLog::Warning("[STL] Not enough memory: can't store normals!");
		mesh->removePerTriangleNormalIndexes();
		mesh->setTriNormsTable(0);
	}

	//progress dialog
	ccProgressDialog pDlg(true);
	CCLib::NormalizedProgress nProgress(&pDlg,faceCount);
	pDlg.setMethodTitle("Loading binary STL file");
	pDlg.setInfo(qPrintable(QString("Loading %1 faces").arg(faceCount)));
	pDlg.start();
	QApplication::processEvents();

	//current vertex shift
	CCVector3d Pshift(0,0,0);

	for (unsigned f=0; f<faceCount; ++f)
	{
		//REAL32[3] Normal vector
		assert(sizeof(float) == 4);
		CCVector3 N;
		if (fp.read((char*)N.u,12) < 12)
			return CC_FERR_READING;

		//3 vertices
		unsigned vertIndexes[3];
//		unsigned pointCountBefore=pointCount;
		for (unsigned i=0; i<3; ++i)
		{
			//REAL32[3] Vertex 1,2 & 3
			float Pf[3];
			if (fp.read((char*)Pf,12) < 0)
				return CC_FERR_READING;

			//first point: check for 'big' coordinates
			CCVector3d Pd( Pf[0], Pf[1], Pf[2] );
			if (pointCount == 0)
			{
				if (HandleGlobalShift(Pd,Pshift,parameters))
				{
					vertices->setGlobalShift(Pshift);
					ccLog::Warning("[STLFilter::loadFile] Cloud has been recentered! Translation: (%.2f,%.2f,%.2f)",Pshift.x,Pshift.y,Pshift.z);
				}
			}

			CCVector3 P = CCVector3::fromArray((Pd + Pshift).u);

			//look for existing vertices at the same place! (STL format is so dumb...)
			{
				//int equivalentIndex = -1;
				//if (pointCount>2)
				//{
				//	//brute force!
				//	for (int j=static_cast<int>(pointCountBefore)-1; j>=0; j--)
				//	{
				//		const CCVector3* Pj = vertices->getPoint(j);
				//		if (Pj->x == P.x &&
				//			Pj->y == P.y &&
				//			Pj->z == P.z)
				//		{
				//			equivalentIndex = j;
				//			break;
				//		}
				//	}
				//}

				////new point ?
				//if (equivalentIndex < 0)
				{
					//cloud is already full?
					if (vertices->capacity() == pointCount && !vertices->reserve(pointCount+1000))
						return CC_FERR_NOT_ENOUGH_MEMORY;

					//insert new point
					vertIndexes[i] = pointCount++;
					vertices->addPoint(P);
				}
				//else
				//{
				//	vertIndexes[i] = static_cast<unsigned>(equivalentIndex);
				//}
			}
		}

		//UINT16 Attribute byte count (not used)
		{
			char a[2];
			if (fp.read(a,2) < 0)
				return CC_FERR_READING;
		}

		//we have successfully read the 3 vertices
		//let's add a new triangle
		{
			mesh->addTriangle(vertIndexes[0],vertIndexes[1],vertIndexes[2]);
		}

		//and a new normal?
		if (normals)
		{
			//compress normal
			int index = static_cast<int>(normals->currentSize());
			CompressedNormType nIndex = ccNormalVectors::GetNormIndex(N.u);
			normals->addElement(nIndex);
			mesh->addTriangleNormalIndexes(index,index,index);
		}

		//progress
		if (!nProgress.oneStep())
			break;
	}

	pDlg.stop();

	return CC_FERR_NO_ERROR;
}

