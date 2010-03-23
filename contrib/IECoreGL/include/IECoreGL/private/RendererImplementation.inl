#ifndef IECOREGL_RENDERERIMPLEMENTATION_INL
#define IECOREGL_RENDERERIMPLEMENTATION_INL

namespace IECoreGL
{

template <class T>
typename T::Ptr RendererImplementation::getState()
{
	return IECore::staticPointerCast<T>( getState( T::staticTypeId() ) );
}

template <class T>
typename T::Ptr RendererImplementation::getUserAttribute( const IECore::InternedString &name )
{
	return IECore::dynamicPointerCast<T>( getUserAttribute( name ) );
}

} // namespace IECoreGL

#endif // IECOREGL_RENDERERIMPLEMENTATION_H
