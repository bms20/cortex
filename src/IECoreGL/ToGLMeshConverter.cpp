//////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2008-2012, Image Engine Design Inc. All rights reserved.
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

#include <cassert>

#include "boost/format.hpp"

#include "IECoreGL/ToGLMeshConverter.h"
#include "IECoreGL/MeshPrimitive.h"

#include "IECore/MeshPrimitive.h"
#include "IECore/TriangulateOp.h"
#include "IECore/MeshNormalsOp.h"
#include "IECore/DespatchTypedData.h"
#include "IECore/MessageHandler.h"

using namespace IECoreGL;

IE_CORE_DEFINERUNTIMETYPED( ToGLMeshConverter );

ToGLConverter::ConverterDescription<ToGLMeshConverter> ToGLMeshConverter::g_description;

ToGLMeshConverter::ToGLMeshConverter( IECore::ConstMeshPrimitivePtr toConvert )
	:	ToGLConverter( "Converts IECore::MeshPrimitive objects to IECoreGL::MeshPrimitive objects.", IECore::MeshPrimitiveTypeId )
{
	srcParameter()->setValue( IECore::constPointerCast<IECore::MeshPrimitive>( toConvert ) );
}

ToGLMeshConverter::~ToGLMeshConverter()
{
}

IECore::RunTimeTypedPtr ToGLMeshConverter::doConversion( IECore::ConstObjectPtr src, IECore::ConstCompoundObjectPtr operands ) const
{
	IECore::MeshPrimitivePtr mesh = IECore::staticPointerCast<IECore::MeshPrimitive>( src->copy() ); // safe because the parameter validated it for us

	if( mesh->interpolation() != "linear" )
	{
		// it's a subdivision mesh. in the absence of a nice subdivision algorithm to display things with,
		// we can at least make things look a bit nicer by calculating some smooth shading normals.
		// if interpolation is linear and no normals are provided then we assume the faceted look is intentional.
		if( mesh->variables.find( "N" )==mesh->variables.end() )
		{
			IECore::MeshNormalsOpPtr normalOp = new IECore::MeshNormalsOp();
			normalOp->inputParameter()->setValue( mesh );
			normalOp->copyParameter()->setTypedValue( false );
			normalOp->operate();
		}
	}
		
	IECore::TriangulateOpPtr op = new IECore::TriangulateOp();
	op->inputParameter()->setValue( mesh );
	op->throwExceptionsParameter()->setTypedValue( false ); // it's better to see something than nothing

	mesh = IECore::runTimeCast< IECore::MeshPrimitive > ( op->operate() );
	assert( mesh );

	IECore::ConstV3fVectorDataPtr p = 0;
	IECore::PrimitiveVariableMap::const_iterator pIt = mesh->variables.find( "P" );
	if( pIt!=mesh->variables.end() )
	{
		if( pIt->second.interpolation==IECore::PrimitiveVariable::Vertex )
		{
			p = IECore::runTimeCast<const IECore::V3fVectorData>( pIt->second.data );
		}
	}
	if( !p )
	{
		throw IECore::Exception( "Must specify primitive variable \"P\", of type V3fVectorData and interpolation type Vertex." );
	}

	MeshPrimitivePtr glMesh = new MeshPrimitive( mesh->vertexIds() );

	for ( IECore::PrimitiveVariableMap::iterator pIt = mesh->variables.begin(); pIt != mesh->variables.end(); ++pIt )
	{
		if ( pIt->second.data )
		{
			glMesh->addPrimitiveVariable( pIt->first, pIt->second );
		}
		else
		{
			IECore::msg( IECore::Msg::Warning, "ToGLMeshConverter", boost::format( "No data given for primvar \"%s\"" ) % pIt->first );
		}
	}

	IECore::PrimitiveVariableMap::const_iterator sIt = mesh->variables.find( "s" );
	IECore::PrimitiveVariableMap::const_iterator tIt = mesh->variables.find( "t" );
	if ( sIt != mesh->variables.end() && tIt != mesh->variables.end() )
	{
		if ( sIt->second.interpolation != IECore::PrimitiveVariable::Constant  
			&&  tIt->second.interpolation != IECore::PrimitiveVariable::Constant
			&& sIt->second.interpolation == tIt->second.interpolation )
		{
			IECore::ConstFloatVectorDataPtr s = IECore::runTimeCast< const IECore::FloatVectorData >( sIt->second.data );
			IECore::ConstFloatVectorDataPtr t = IECore::runTimeCast< const IECore::FloatVectorData >( tIt->second.data );

			if ( s && t )
			{
				/// Should hold true if primvarsAreValid
				assert( s->readable().size() == t->readable().size() );

				IECore::V2fVectorDataPtr stData = new IECore::V2fVectorData();
				stData->writable().resize( s->readable().size() );

				for ( unsigned i = 0; i < s->readable().size(); i++ )
				{
					stData->writable()[i] = Imath::V2f( s->readable()[i], t->readable()[i] );
				}
				glMesh->addPrimitiveVariable( "st", IECore::PrimitiveVariable( sIt->second.interpolation, stData ) );
			}
			else
			{
				IECore::msg( IECore::Msg::Warning, "ToGLMeshConverter", "If specified, primitive variables \"s\" and \"t\" must be of type FloatVectorData and interpolation type FaceVarying." );
			}
		}
		else
		{
			IECore::msg( IECore::Msg::Warning, "ToGLMeshConverter", "If specified, primitive variables \"s\" and \"t\" must be of type FloatVectorData and non-Constant interpolation type." );
		}
	}
	else if ( sIt != mesh->variables.end() || tIt != mesh->variables.end() )
	{
		IECore::msg( IECore::Msg::Warning, "ToGLMeshConverter", "Primitive variable \"s\" or \"t\" found, but not both." );
	}

	return glMesh;
}
