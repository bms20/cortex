//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2012-2013, Image Engine Design Inc. All rights reserved.
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

#include "PRM/PRM_ChoiceList.h"
#include "PRM/PRM_Default.h"
#include "UT/UT_StringMMPattern.h"

#include "IECore/CoordinateSystem.h"
#include "IECore/Group.h"
#include "IECore/TransformOp.h"
#include "IECore/VisibleRenderable.h"

#include "IECoreHoudini/Convert.h"
#include "IECoreHoudini/SOP_SceneCacheSource.h"
#include "IECoreHoudini/ToHoudiniAttribConverter.h"
#include "IECoreHoudini/ToHoudiniGeometryConverter.h"

using namespace IECore;
using namespace IECoreHoudini;

static InternedString pName( "P" );

const char *SOP_SceneCacheSource::typeName = "ieSceneCacheSource";

PRM_Name SOP_SceneCacheSource::pShapeFilter( "shapeFilter", "Shape Filter" );
PRM_Name SOP_SceneCacheSource::pAttributeFilter( "attributeFilter", "Attribute Filter" );

PRM_Default SOP_SceneCacheSource::shapeFilterDefault( 0, "*" );
PRM_Default SOP_SceneCacheSource::attributeFilterDefault( 0, "*" );

PRM_ChoiceList SOP_SceneCacheSource::shapeFilterMenu( PRM_CHOICELIST_TOGGLE, &SOP_SceneCacheSource::buildShapeFilterMenu );

OP_TemplatePair *SOP_SceneCacheSource::buildParameters()
{
	static PRM_Template *thisTemplate = 0;
	if ( !thisTemplate )
	{
		unsigned numSCCParms = PRM_Template::countTemplates( SceneCacheNode<SOP_Node>::parameters );
		thisTemplate = new PRM_Template[ numSCCParms + 3 ];
		
		// add the file parms
		for ( unsigned i = 0; i < 3; ++i )
		{
			thisTemplate[i] = SceneCacheNode<SOP_Node>::parameters[i];
		}
		
		// then the filters
		thisTemplate[3] = PRM_Template(
			PRM_STRING, 1, &pShapeFilter, &shapeFilterDefault, &shapeFilterMenu, 0, 0, 0, 0,
			"A list of filters to decide which shapes to load. Uses Houdini matching syntax"
		);
		thisTemplate[4] = PRM_Template(
			PRM_STRING, 1, &pAttributeFilter, &attributeFilterDefault, 0, 0, 0, 0, 0,
			"A list of attribute names to load, if they exist on each shape. Uses Houdini matching syntax. "
			"P will always be loaded."
		);
		
		// then the rest
		for ( unsigned i = 3; i < numSCCParms; ++i )
		{
			thisTemplate[2+i] = SceneCacheNode<SOP_Node>::parameters[i];
		}
	}
	
	static OP_TemplatePair *templatePair = 0;
	if ( !templatePair )
	{
		templatePair = new OP_TemplatePair( thisTemplate );
	}
	
	return templatePair;
}

SOP_SceneCacheSource::SOP_SceneCacheSource( OP_Network *net, const char *name, OP_Operator *op ) : SceneCacheNode<SOP_Node>( net, name, op )
{
}

SOP_SceneCacheSource::~SOP_SceneCacheSource()
{
}

OP_Node *SOP_SceneCacheSource::create( OP_Network *net, const char *name, OP_Operator *op )
{
	return new SOP_SceneCacheSource( net, name, op );
}

void SOP_SceneCacheSource::buildShapeFilterMenu( void *data, PRM_Name *menu, int maxSize, const PRM_SpareData *, const PRM_Parm * )
{
	SOP_SceneCacheSource *node = reinterpret_cast<SOP_SceneCacheSource*>( data );
	if ( !node )
	{
		return;
	}
	
	menu[0].setToken( "*" );
	menu[0].setLabel( "*" );
	
	std::string file;
	if ( !node->ensureFile( file ) )
	{
		// mark the end of our menu
		menu[1].setToken( 0 );
		return;
	}
	
	ConstSceneInterfacePtr scene = node->scene( file, node->getPath() );
	if ( !scene )
	{
		// mark the end of our menu
		menu[1].setToken( 0 );
		return;
	}
	
	std::vector<std::string> objects;
	node->objectNames( scene, objects );
	node->createMenu( menu, objects );
}

OP_ERROR SOP_SceneCacheSource::cookMySop( OP_Context &context )
{
	flags().setTimeDep( true );
	
	std::string file;
	if ( !ensureFile( file ) )
	{
		addError( SOP_ATTRIBUTE_INVALID, ( file + " is not a valid .scc" ).c_str() );
		gdp->clearAndDestroy();
		return error();
	}
	
	std::string path = getPath();
	Space space = getSpace();
	
	UT_String shapeFilterStr;
	evalString( shapeFilterStr, pShapeFilter.getToken(), 0, 0 );
	UT_StringMMPattern shapeFilter;
	shapeFilter.compile( shapeFilterStr );
	
	UT_String p( "P" );
	UT_String attributeFilterStr;
	evalString( attributeFilterStr, pAttributeFilter.getToken(), 0, 0 );
	UT_StringMMPattern attributeFilter;
	if ( !p.match( attributeFilterStr ) )
	{
		attributeFilterStr += " P";
	}
	attributeFilter.compile( attributeFilterStr );
	
	ConstSceneInterfacePtr scene = this->scene( file, path );
	if ( !scene )
	{
		addError( SOP_ATTRIBUTE_INVALID, ( path + " is not a valid location in " + file ).c_str() );
		gdp->clearAndDestroy();
		return error();
	}
	
	MurmurHash hash;
	hash.append( file );
	hash.append( path );
	hash.append( space );
	hash.append( shapeFilterStr );
	hash.append( attributeFilterStr );
	
	if ( !m_loaded || m_hash != hash )
	{
		gdp->clearAndDestroy();
	}
	
	/// \todo: need to clearAndDestroy if any shapes have changing topology
	
	Imath::M44d transform = ( space == World ) ? worldTransform( file, path, context.getTime() ) : Imath::M44d();
	
	loadObjects( scene, transform, context.getTime(), space, shapeFilter, attributeFilter );
	
	m_loaded = true;
	m_hash = hash;
	
	return error();
}

void SOP_SceneCacheSource::loadObjects( const IECore::SceneInterface *scene, Imath::M44d transform, double time, Space space, const UT_StringMMPattern &shapeFilter, const UT_StringMMPattern &attributeFilter )
{
	if ( scene->hasObject() && UT_String( scene->name() ).multiMatch( shapeFilter ) )
	{
		ObjectPtr object = scene->readObject( time );
		
		std::string fullName;
		SceneInterface::Path p;
		scene->path( p );
		SceneInterface::pathToString( p, fullName );
		
		modifyObject( object, fullName, attributeFilter );
		
		Imath::M44d currentTransform;
		if ( space == Local )
		{
			currentTransform = scene->readTransformAsMatrix( time );
		}
		else if ( space != Object )
		{
			currentTransform = transform;
		}
		
		transformObject( object, currentTransform );
		
		if ( !convertObject( object, fullName, scene ) )
		{
			addError( SOP_LOAD_UNKNOWN_BINARY_FLAG, ( "Could not convert " + fullName + " to houdini" ).c_str() );
		}
	}
	
	SceneInterface::NameList children;
	scene->childNames( children );
	for ( SceneInterface::NameList::const_iterator it=children.begin(); it != children.end(); ++it )
	{
		ConstSceneInterfacePtr child = scene->child( *it );
		loadObjects( child, child->readTransformAsMatrix( time ) * transform, time, space, shapeFilter, attributeFilter );
	}
}

IECore::ObjectPtr SOP_SceneCacheSource::modifyObject( IECore::Object *object, std::string &name, const UT_StringMMPattern &attributeFilter )
{
	VisibleRenderable *renderable = IECore::runTimeCast<VisibleRenderable>( object );
	if ( !renderable )
	{
		return object;
	}
	
	renderable->blindData()->member<StringData>( "name", false, true )->writable() = name;
	
	Primitive *primitive = IECore::runTimeCast<Primitive>( renderable );
	if ( primitive )
	{
		PrimitiveVariableMap &variables = primitive->variables;
		PrimitiveVariableMap::iterator it = variables.begin();
		PrimitiveVariableMap::iterator toErase = variables.begin();
		while ( it != variables.end() )
		{
			UT_String attributeName( it->first );
			if ( !attributeName.multiMatch( attributeFilter ) )
			{
				variables.erase( it++ );
			}
			else
			{
				it++;
			}
		}
	}
	
	return object;
};

IECore::ObjectPtr SOP_SceneCacheSource::transformObject( IECore::Object *object, Imath::M44d transform )
{
	Primitive *primitive = IECore::runTimeCast<Primitive>( object );
	if ( primitive )
	{
		TransformOpPtr transformer = new TransformOp();
		transformer->inputParameter()->setValue( primitive );
		transformer->copyParameter()->setTypedValue( false );
		transformer->matrixParameter()->setValue( new M44dData( transform ) );
		return transformer->operate();
	}
	
	Group *group = IECore::runTimeCast<Group>( object );
	if ( group )
	{
		group->setTransform( matrixTransform( transform ) );
		return group;
	}
	
	CoordinateSystem *coord = IECore::runTimeCast<CoordinateSystem>( object );
	if ( coord )
	{
		coord->setTransform( matrixTransform( transform ) );
		return coord;
	}
	
	return object;
}

bool SOP_SceneCacheSource::convertObject( IECore::Object *object, std::string &name, const IECore::SceneInterface *scene )
{
	VisibleRenderable *renderable = IECore::runTimeCast<VisibleRenderable>( object );
	if ( !renderable )
	{
		return false;
	}
	
	// attempt to optimize the conversion by re-using animated primitive variables
	/// \todo: this doesn't account for objects with transforms (P will be changing, but wont be in the attribute)
	const Primitive *primitive = IECore::runTimeCast<Primitive>( renderable );
	if ( primitive && !scene->hasAttribute( SceneCache::animatedObjectTopologyAttribute ) && scene->hasAttribute( SceneCache::animatedObjectPrimVarsAttribute ) )
	{
		const ObjectPtr primVarObj = scene->readAttribute( SceneCache::animatedObjectPrimVarsAttribute, 0 );
		const InternedStringVectorData *primVarData = IECore::runTimeCast<const InternedStringVectorData>( primVarObj );
		GA_ROAttributeRef nameAttrRef = gdp->findStringTuple( GA_ATTRIB_PRIMITIVE, "name" );
		GA_Range primRange = gdp->getRangeByValue( nameAttrRef, name.c_str() );
		if ( primVarData && nameAttrRef.isValid() && !primRange.isEmpty() )
		{
			const std::vector<InternedString> &primVars = primVarData->readable();
			// this means constant topology and primitive variables, even though multiple samples were written
			if ( primVars.empty() )
			{
				return true;
			}
			
			bool optimized = false;
			GA_Range pointRange( *gdp, primRange, GA_ATTRIB_POINT, GA_Range::primitiveref(), false );
			for ( std::vector<InternedString>::const_iterator it = primVars.begin(); it != primVars.end(); ++it )
			{
				// special case for P unfortunately
				if ( *it == pName )
				{
					const V3fVectorData *positions = primitive->variableData<V3fVectorData>( pName.string() );
					if ( !positions )
					{
						continue;
					}
					
					const std::vector<Imath::V3f> &pos = positions->readable();
					if ( (size_t)pointRange.getEntries() != pos.size() )
					{
						continue;
					}
					
					size_t i = 0;
					for ( GA_Iterator it=pointRange.begin(); !it.atEnd(); ++it, ++i )
					{
						gdp->setPos3( it.getOffset(), IECore::convert<UT_Vector3>( pos[i] ) );
					}
					
					optimized = true;
				}
				else
				{
					PrimitiveVariableMap::const_iterator pIt = primitive->variables.find( it->string() );
					if ( pIt == primitive->variables.end() )
					{
						continue;
					}
					
					ToHoudiniAttribConverterPtr converter = ToHoudiniAttribConverter::create( pIt->second.data );
					if ( !converter )
					{
						continue;
					}
					
					if ( pIt->second.interpolation == PrimitiveVariable::Vertex )
					{
						converter->convert( pIt->first, gdp, pointRange );
						optimized = true;
					}
					else if ( pIt->second.interpolation == PrimitiveVariable::Uniform )
					{
						converter->convert( pIt->first, gdp, primRange );
						optimized = true;
					}
					else
					{
						addWarning( SOP_ATTRIBUTE_INVALID, ( pIt->first + " could not be converted for " + name + ". Only Vertex and Uniform variables can be optimized." ).c_str() );
					}
				}
			}
			
			if ( optimized )
			{
				return true;
			}
		}
	}
	
	// fallback to full conversion
	ToHoudiniGeometryConverterPtr converter = ToHoudiniGeometryConverter::create( renderable );
	if ( converter && converter->convert( myGdpHandle ) )
	{
		return true;
	}
	
	return false;
}

MatrixTransformPtr SOP_SceneCacheSource::matrixTransform( Imath::M44d t )
{
	return new MatrixTransform(
		Imath::M44f(
			t[0][0], t[0][1], t[0][2], t[0][3],
			t[1][0], t[1][1], t[1][2], t[1][3],
			t[2][0], t[2][1], t[2][2], t[2][3],
			t[3][0], t[3][1], t[3][2], t[3][3]
		)
	);
}