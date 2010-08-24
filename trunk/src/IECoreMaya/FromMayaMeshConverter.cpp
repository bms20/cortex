//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2007-2009, Image Engine Design Inc. All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions are
//  met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of Image Engine Design nor the names of any
//       other contributors to this software may be used to endorse or
//       promote products derived from this software without specific prior
//       written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
//  IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
//  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
//  PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
//  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
//  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
//  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
//  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
//  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
//  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//////////////////////////////////////////////////////////////////////////

#include "IECoreMaya/FromMayaMeshConverter.h"
#include "IECoreMaya/FromMayaPlugConverter.h"
#include "IECoreMaya/MArrayIter.h"
#include "IECoreMaya/VectorTraits.h"

#include "IECore/MeshPrimitive.h"
#include "IECore/VectorOps.h"
#include "IECore/CompoundParameter.h"
#include "IECore/NumericParameter.h"
#include "IECore/MessageHandler.h"
#include "IECore/DespatchTypedData.h"

#include "maya/MFn.h"
#include "maya/MFnMesh.h"
#include "maya/MFnAttribute.h"
#include "maya/MString.h"

#include <algorithm>

using namespace IECoreMaya;
using namespace IECore;
using namespace std;
using namespace Imath;

IE_CORE_DEFINERUNTIMETYPED( FromMayaMeshConverter );

static const MFn::Type fromTypes[] = { MFn::kMesh, MFn::kMeshData, MFn::kInvalid };
static const IECore::TypeId toTypes[] = { BlindDataHolderTypeId, RenderableTypeId, VisibleRenderableTypeId, PrimitiveTypeId, MeshPrimitiveTypeId, InvalidTypeId };

FromMayaShapeConverter::Description<FromMayaMeshConverter> FromMayaMeshConverter::m_description( fromTypes, toTypes );

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// structors
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FromMayaMeshConverter::FromMayaMeshConverter( const MObject &object )
	:	FromMayaShapeConverter( "Converts poly meshes to IECore::MeshPrimitive objects.", object )
{
	constructCommon();
}

FromMayaMeshConverter::FromMayaMeshConverter( const MDagPath &dagPath )
	:	FromMayaShapeConverter( "Converts poly meshes to IECore::MeshPrimitive objects.", dagPath )
{
	constructCommon();
}

void FromMayaMeshConverter::constructCommon()
{

	// interpolation
	StringParameter::PresetsContainer interpolationPresets;
	interpolationPresets.push_back( StringParameter::Preset( "poly", "linear" ) );
	interpolationPresets.push_back( StringParameter::Preset( "subdiv", "catmullClark" ) );

	m_interpolation = new StringParameter(
		"interpolation",
		"Sets the interpolation type of the new mesh",
		"linear",
		interpolationPresets
	);

	parameters()->addParameter( m_interpolation );

	// points
	BoolParameter::PresetsContainer pointsPresets;
	pointsPresets.push_back( BoolParameter::Preset( "poly", true ) );
	pointsPresets.push_back( BoolParameter::Preset( "subdiv", true ) );

	m_points = new BoolParameter(
		"points",
		"When this is on the mesh points are added to the result as a primitive variable named \"P\".",
		true,
		pointsPresets
	);

	parameters()->addParameter( m_points );

	// normals
	BoolParameter::PresetsContainer normalsPresets;
	normalsPresets.push_back( BoolParameter::Preset( "poly", true ) );
	normalsPresets.push_back( BoolParameter::Preset( "subdiv", true ) );

	m_normals = new BoolParameter(
		"normals",
		"When this is on the mesh normals are added to the result as a primitive variable named \"N\". "
		"Note that normals will only ever be added to meshes created with linear interpolation as "
		"vertex normals are unsuitable for meshes which will be rendered with some form of "
		"subdivision.",
		true,
		normalsPresets
	);

	parameters()->addParameter( m_normals );

	// st
	BoolParameter::PresetsContainer stPresets;
	stPresets.push_back( BoolParameter::Preset( "poly", true ) );
	stPresets.push_back( BoolParameter::Preset( "subdiv", true ) );

	m_st = new BoolParameter(
		"st",
		"When this is on the default uv set is added to the result as primitive variables named \"s\" and \"t\".",
		true,
		stPresets
	);

	parameters()->addParameter( m_st );

	// extra st
	BoolParameter::PresetsContainer extraSTPresets;
	extraSTPresets.push_back( BoolParameter::Preset( "poly", true ) );
	extraSTPresets.push_back( BoolParameter::Preset( "subdiv", true ) );

	m_extraST = new BoolParameter(
		"extraST",
		"When this is on, all uv sets are added to the result as primitive variables named \"setName_s\" and \"setName_t\".",
		true,
		extraSTPresets
	);

	parameters()->addParameter( m_extraST );

}

FromMayaMeshConverter::~FromMayaMeshConverter()
{
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// parameter access
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IECore::StringParameterPtr FromMayaMeshConverter::interpolationParameter()
{
	return m_interpolation;
}

IECore::StringParameterPtr FromMayaMeshConverter::interpolationParameter() const
{
	return m_interpolation;
}

IECore::BoolParameterPtr FromMayaMeshConverter::pointsParameter()
{
	return m_points;
}

IECore::BoolParameterPtr FromMayaMeshConverter::pointsParameter() const
{
	return m_points;
}

IECore::BoolParameterPtr FromMayaMeshConverter::normalsParameter()
{
	return m_normals;
}

IECore::BoolParameterPtr FromMayaMeshConverter::normalsParameter() const
{
	return m_normals;
}

IECore::BoolParameterPtr FromMayaMeshConverter::stParameter()
{
	return m_st;
}

IECore::BoolParameterPtr FromMayaMeshConverter::stParameter() const
{
	return m_st;
}

IECore::BoolParameterPtr FromMayaMeshConverter::extraSTParameter()
{
	return m_extraST;
}

IECore::BoolParameterPtr FromMayaMeshConverter::extraSTParameter() const
{
	return m_extraST;
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// conversion
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

IECore::V3fVectorDataPtr FromMayaMeshConverter::points() const
{
	MFnMesh fnMesh;
	const MDagPath *d = dagPath( true );
	if( d )
	{
		fnMesh.setObject( *d );
	}
	else
	{
		fnMesh.setObject( object() );
	}

	MFloatPointArray mPoints;
	fnMesh.getPoints( mPoints, space() );

	V3fVectorDataPtr points = new V3fVectorData;
	points->writable().resize( mPoints.length() );
	std::transform( MArrayIter<MFloatPointArray>::begin( mPoints ), MArrayIter<MFloatPointArray>::end( mPoints ), points->writable().begin(), VecConvert<MFloatPoint, V3f>() );
	return points;
}

IECore::V3fVectorDataPtr FromMayaMeshConverter::normals() const
{
	MFnMesh fnMesh;
	const MDagPath *d = dagPath( true );
	if( d )
	{
		fnMesh.setObject( *d );
	}
	else
	{
		fnMesh.setObject( object() );
	}

	V3fVectorDataPtr normalsData = new V3fVectorData;
	vector<V3f> &normals = normalsData->writable();
	normals.resize( fnMesh.numFaceVertices() );

	int numPolygons = fnMesh.numPolygons();
	MFloatVectorArray faceNormals;
	unsigned int normalIndex = 0;
	for( int i=0; i<numPolygons; i++ )
	{
		fnMesh.getFaceVertexNormals( i, faceNormals, space() );
		for( unsigned j=0; j<faceNormals.length(); j++ )
		{
			normals[normalIndex++] = vecConvert<MVector, V3f>( faceNormals[j] );
		}
	}
	assert( normalIndex==normals.size() );
	return normalsData;
}

IECore::FloatVectorDataPtr FromMayaMeshConverter::sOrT( const MString &uvSet, unsigned int index ) const
{
	MFnMesh fnMesh( object() );
	FloatVectorDataPtr resultData = new FloatVectorData;
	vector<float> &result = resultData->writable();
	result.resize( fnMesh.numFaceVertices() );
	int numPolygons = fnMesh.numPolygons();
	unsigned int resultIndex = 0;
	for( int i=0; i<numPolygons; i++ )
	{
		for( int j=0; j<fnMesh.polygonVertexCount( i ); j++ )
		{
			float uv[2];
			fnMesh.getPolygonUV( i, j, uv[0], uv[1], &uvSet );
			result[resultIndex++] = index==1 ? 1-uv[index] : uv[index];
		}
	}
	return resultData;

}

IECore::FloatVectorDataPtr FromMayaMeshConverter::s( const MString &uvSet ) const
{
	return sOrT( uvSet, 0 );
}

IECore::FloatVectorDataPtr FromMayaMeshConverter::t( const MString &uvSet ) const
{
	return sOrT( uvSet, 1 );
}

IECore::IntVectorDataPtr FromMayaMeshConverter::stIndices( const MString &uvSet ) const
{
	MFnMesh fnMesh( object() );
	IntVectorDataPtr resultData = new IntVectorData;
	vector<int> &result = resultData->writable();
	result.resize( fnMesh.numFaceVertices() );
	int numPolygons = fnMesh.numPolygons();
	unsigned int resultIndex = 0;
	for( int i=0; i<numPolygons; i++ )
	{
		for( int j=0; j<fnMesh.polygonVertexCount( i ); j++ )
		{
			fnMesh.getPolygonUVid( i, j, result[resultIndex++], &uvSet );
		}
	}
	return resultData;
}

IECore::PrimitivePtr FromMayaMeshConverter::doPrimitiveConversion( const MObject &object, IECore::ConstCompoundObjectPtr operands ) const
{
	MFnMesh fnMesh( object );
	return doPrimitiveConversion( fnMesh );
}

IECore::PrimitivePtr FromMayaMeshConverter::doPrimitiveConversion( const MDagPath &dagPath, IECore::ConstCompoundObjectPtr operands ) const
{
	MFnMesh fnMesh( dagPath );
	return doPrimitiveConversion( fnMesh );
}

IECore::PrimitivePtr FromMayaMeshConverter::doPrimitiveConversion( MFnMesh &fnMesh ) const
{
	// get basic topology and create a mesh
	int numPolygons = fnMesh.numPolygons();
	IntVectorDataPtr verticesPerFaceData = new IntVectorData;
	verticesPerFaceData->writable().resize( numPolygons );
	vector<int>::iterator verticesPerFaceIt = verticesPerFaceData->writable().begin();

	IntVectorDataPtr vertexIds = new IntVectorData;
	vertexIds->writable().resize( fnMesh.numFaceVertices() );
	vector<int>::iterator vertexIdsIt = vertexIds->writable().begin();

	MIntArray polygonVertices;
	for( int i=0; i<numPolygons; i++ )
	{
		fnMesh.getPolygonVertices( i, polygonVertices );
		*verticesPerFaceIt++ = polygonVertices.length();
		copy( MArrayIter<MIntArray>::begin( polygonVertices ), MArrayIter<MIntArray>::end( polygonVertices ), vertexIdsIt );
		vertexIdsIt += polygonVertices.length();
	}

	MeshPrimitivePtr result = new MeshPrimitive( verticesPerFaceData, vertexIds, m_interpolation->getTypedValue() );

	if( m_points->getTypedValue() )
	{
		result->variables["P"] = PrimitiveVariable( PrimitiveVariable::Vertex, points() );
	}
	if( m_normals->getTypedValue() && m_interpolation->getTypedValue()=="linear" )
	{
		result->variables["N"] = PrimitiveVariable( PrimitiveVariable::FaceVarying, normals() );
	}

	MString currentUVSet;
	fnMesh.getCurrentUVSetName( currentUVSet );
	MStringArray uvSets;
	fnMesh.getUVSetNames( uvSets );
	for( unsigned int i=0; i<uvSets.length(); i++ )
	{
		FloatVectorDataPtr sData = s( uvSets[i] );
		FloatVectorDataPtr tData = t( uvSets[i] );
		IntVectorDataPtr stIndicesData = stIndices( uvSets[i] );
		if( uvSets[i]==currentUVSet )
		{
			if( m_st->getTypedValue() )
			{
				result->variables["s"] = PrimitiveVariable( PrimitiveVariable::FaceVarying, sData );
				result->variables["t"] = PrimitiveVariable( PrimitiveVariable::FaceVarying, tData );
				result->variables["stIndices"] = PrimitiveVariable( PrimitiveVariable::FaceVarying, stIndicesData );
			}
		}
		
		if( m_extraST->getTypedValue() )
		{
			MString sName = uvSets[i] + "_s";
			MString tName = uvSets[i] + "_t";
			MString indicesName = uvSets[i] + "Indices";
			result->variables[sName.asChar()] = PrimitiveVariable( PrimitiveVariable::FaceVarying, sData );
			result->variables[tName.asChar()] = PrimitiveVariable( PrimitiveVariable::FaceVarying, tData );
			result->variables[indicesName.asChar()] = PrimitiveVariable( PrimitiveVariable::FaceVarying, stIndicesData );
		}
	}

	return result;
}