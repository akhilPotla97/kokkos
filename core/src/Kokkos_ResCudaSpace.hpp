/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 2.0
//              Copyright (2014) Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Christian R. Trott (crtrott@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_RESCUDASPACE_HPP
#define KOKKOS_RESCUDASPACE_HPP

#include <Kokkos_Macros.hpp>
#if defined( KOKKOS_ENABLE_CUDA )

#include <Kokkos_CudaSpace.hpp>
#include <cmath>
#include <map>

/*--------------------------------------------------------------------------*/

namespace Kokkos {

namespace Experimental {

template<class Type, class Enabled = void>
struct MergeFunctor;

template<class Type>
struct MergeFunctor<Type, typename std::enable_if< std::is_same< Type, float >::value ||
                                          std::is_same< Type, double >::value, void >::type > {
  
   KOKKOS_INLINE_FUNCTION
   MergeFunctor() {}

   KOKKOS_INLINE_FUNCTION
   MergeFunctor(const MergeFunctor & mf) {}

   KOKKOS_INLINE_FUNCTION
   bool compare( Type a, Type b ) const {
      return (abs(a-b)<0.00000001);
   }
};

template<class Type>
struct MergeFunctor<Type, typename std::enable_if< !std::is_same< Type, float >::value &&
                                          !std::is_same< Type, double >::value, void >::type > {

   KOKKOS_INLINE_FUNCTION
   MergeFunctor() {}

   KOKKOS_INLINE_FUNCTION
   MergeFunctor(const MergeFunctor & mf) {}

   KOKKOS_INLINE_FUNCTION
   bool compare( Type a, Type b ) const {
      return (a == b);
   }
};

class DuplicateTracker {
public:
   void * original_data;
   int dup_cnt;
   int data_len;
   void * dup_list[3];
   void * func_ptr;

   inline __host__ virtual ~DuplicateTracker() {}

   inline __host__
   DuplicateTracker() : original_data(nullptr) { 
      dup_cnt = 0;
      data_len = 0;
      for (int i = 0; i < 3; i++) { dup_list[i] = nullptr; }
   }
   
   inline __host__
   DuplicateTracker(const DuplicateTracker & dt) : original_data( dt.original_data ) {
      dup_cnt = dt.dup_cnt;
      data_len = dt.data_len;
      for (int i = 0; i < dup_cnt; i++) { dup_list[i] = dt.dup_list[i]; }
   }

   inline __host__
   void add_dup( Kokkos::Impl::SharedAllocationRecord<void,void>* dup ) {
      if (dup_cnt < 3) {
         dup_list[dup_cnt] = (void*)dup->data();
         dup_cnt++;
         printf("duplicate added to list: %d\n", dup_cnt);
      }
   }

   inline __host__
   virtual void combine_dups() {
   }
};

template<class DType, class ExecSpace> 
class CombineFunctor {
public:
   typedef MergeFunctor<DType> functor_type;
   functor_type cf;
 
   DType* orig_view;
   DType* dup_view[3];
   size_t m_len;

   static void * s_dup_kernel;

   inline __host__ 
   CombineFunctor() : orig_view(nullptr), dup_view{}, m_len (0) {}

   inline __host__ void load_ptrs( DType * orig, DType * d1, DType * d2, DType * d3, size_t len) {
       orig_view = orig;
       dup_view[0] = d1;
       dup_view[1] = d2;
       dup_view[2] = d3;
       m_len = len;
   }

   KOKKOS_INLINE_FUNCTION
   int get_len() const {
      return (int)m_len;
   }

   KOKKOS_INLINE_FUNCTION 
   CombineFunctor( const CombineFunctor & rhs ) : orig_view(rhs.orig_view), dup_view{} {
      for (int i = 0; i < 3; i++)
          dup_view[i] = rhs.dup_view[i];
      m_len = rhs.m_len;
   }
   
   KOKKOS_INLINE_FUNCTION
   //void operator ()(const int i) const {
   void exec(const int i) const {
      printf("combine dups: %d\n", i);
      for (int j = 0; j < 3; j++) {
         printf("iterating outer: %d - %d \n", i, j);
         orig_view[i]  =  dup_view[j][i];
         printf("first entry: %d, %d\n",j, orig_view[i]);
         int k = j < 2 ? j+1 : 0;
         for ( int r = 0; r < 2 ; r++) {
            printf("iterate inner %d, %d, %d \n", i, j, k);
            if ( cf.compare( dup_view[k][i], orig_view[i] ) )  // just need 2 that are the same
            {
               printf("match found: %d - %d\n", i, j);
               return;
            }
            k = k < 2 ? k+1 : 0;
         }
      }
      printf("no match found: %i\n", i);
   }

};

template<class Type, class ExecutionSpace>
class SpecDuplicateTracker : public DuplicateTracker  {
public:
   typedef typename std::remove_reference<Type>::type nr_type;
   typedef typename std::remove_pointer<nr_type>::type np_type;
   typedef typename std::remove_extent<np_type>::type ne_type;
   typedef typename std::remove_const<ne_type>::type rd_type;
   typedef CombineFunctor<rd_type, ExecutionSpace> comb_type;

   comb_type m_cf;

   inline __host__
   SpecDuplicateTracker() : DuplicateTracker( ), m_cf() { 
   }

   inline __host__
   SpecDuplicateTracker(const SpecDuplicateTracker & rhs) : DuplicateTracker( rhs ), m_cf(rhs.m_cf)  { 
   }
   
   __host__
   virtual void combine_dups();

};

} // Experimental


/** \brief  Cuda on-device memory management */

class ResCudaSpace : public CudaSpace {
public:
  //! Tag this class as a kokkos memory space
  typedef ResCudaSpace             memory_space ;
  typedef ResCudaSpace          resilient_space ;
  typedef Kokkos::Cuda          execution_space ;
  typedef Kokkos::Device<execution_space,memory_space> device_type;

  typedef unsigned int          size_type ;

  /*--------------------------------*/

  ResCudaSpace();
  ResCudaSpace( ResCudaSpace && rhs ) = default ;
  ResCudaSpace( const ResCudaSpace & rhs ) = default ;
  ResCudaSpace & operator = ( ResCudaSpace && rhs ) = default ;
  ResCudaSpace & operator = ( const ResCudaSpace & rhs ) = default ;
  ~ResCudaSpace() = default ;
  
  static void clear_duplicates_list();

  static std::map<std::string, Kokkos::Experimental::DuplicateTracker * > duplicate_map;

private:

  friend class Kokkos::Impl::SharedAllocationRecord< Kokkos::ResCudaSpace , void > ;
};

} // namespace Kokkos

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

namespace Kokkos {
namespace Impl {

static_assert( Kokkos::Impl::MemorySpaceAccess< Kokkos::ResCudaSpace , Kokkos::ResCudaSpace >::assignable , "" );

//----------------------------------------

template<>
struct MemorySpaceAccess< Kokkos::HostSpace , Kokkos::ResCudaSpace > {
  enum { assignable = false };
  enum { accessible = false };
  enum { deepcopy   = true };
};

//----------------------------------------

template<>
struct MemorySpaceAccess< Kokkos::ResCudaSpace , Kokkos::HostSpace > {
  enum { assignable = false };
  enum { accessible = false };
  enum { deepcopy   = true };
};

template<>
struct MemorySpaceAccess< Kokkos::CudaSpace , Kokkos::ResCudaSpace > {
  enum { assignable = true };
  enum { accessible = true };
  enum { deepcopy   = true };
};

//----------------------------------------

template<>
struct MemorySpaceAccess< Kokkos::ResCudaSpace , Kokkos::CudaSpace > {
  enum { assignable = true };
  enum { accessible = true };
  enum { deepcopy   = true };
};

template<>
struct MemorySpaceAccess< Kokkos::ResCudaSpace , Kokkos::CudaUVMSpace > {
  // CudaSpace::execution_space == CudaUVMSpace::execution_space
  enum { assignable = true };
  enum { accessible = true };
  enum { deepcopy   = true };
};

template<>
struct MemorySpaceAccess< Kokkos::ResCudaSpace , Kokkos::CudaHostPinnedSpace > {
  // CudaSpace::execution_space != CudaHostPinnedSpace::execution_space
  enum { assignable = false };
  enum { accessible = true }; // ResCudaSpace::execution_space
  enum { deepcopy   = true };
};

//----------------------------------------
// CudaUVMSpace::execution_space == Cuda
// CudaUVMSpace accessible to both Cuda and Host

template<>
struct MemorySpaceAccess< Kokkos::CudaUVMSpace , Kokkos::ResCudaSpace > {
  // CudaUVMSpace::execution_space == CudaSpace::execution_space
  // Can access CudaUVMSpace from Host but cannot access ResCudaSpace from Host
  enum { assignable = false };

  // CudaUVMSpace::execution_space can access CudaSpace
  enum { accessible = true };
  enum { deepcopy   = true };
};

template<>
struct MemorySpaceAccess< Kokkos::CudaHostPinnedSpace , Kokkos::ResCudaSpace > {
  enum { assignable = false }; // Cannot access from Host
  enum { accessible = false };
  enum { deepcopy   = true };
};

//----------------------------------------

}} // namespace Kokkos::Impl

/*--------------------------------------------------------------------------*/
/*--------------------------------------------------------------------------*/

namespace Kokkos {
namespace Impl {

template<> struct DeepCopy< ResCudaSpace , ResCudaSpace , Cuda>
{
  DeepCopy( void * dst , const void * src , size_t );
  DeepCopy( const Cuda & , void * dst , const void * src , size_t );
};

template<> struct DeepCopy< ResCudaSpace , HostSpace , Cuda >
{
  DeepCopy( void * dst , const void * src , size_t );
  DeepCopy( const Cuda & , void * dst , const void * src , size_t );
};

template<> struct DeepCopy< HostSpace , ResCudaSpace , Cuda >
{
  DeepCopy( void * dst , const void * src , size_t );
  DeepCopy( const Cuda & , void * dst , const void * src , size_t );
};

template<class ExecutionSpace> struct DeepCopy< ResCudaSpace , ResCudaSpace , ExecutionSpace >
{
  inline
  DeepCopy( void * dst , const void * src , size_t n )
  { (void) DeepCopy< ResCudaSpace , ResCudaSpace , Cuda >( dst , src , n ); }

  inline
  DeepCopy( const ExecutionSpace& exec, void * dst , const void * src , size_t n )
  {
    exec.fence();
    DeepCopyAsyncCuda (dst,src,n);
  }
};

template<class ExecutionSpace> struct DeepCopy< ResCudaSpace , HostSpace , ExecutionSpace >
{
  inline
  DeepCopy( void * dst , const void * src , size_t n )
  { (void) DeepCopy< ResCudaSpace , HostSpace , Cuda>( dst , src , n ); }

  inline
  DeepCopy( const ExecutionSpace& exec, void * dst , const void * src , size_t n )
  {
    exec.fence();
    DeepCopyAsyncCuda (dst,src,n);
  }
};

template<class ExecutionSpace>
struct DeepCopy< HostSpace , ResCudaSpace , ExecutionSpace >
{
  inline
  DeepCopy( void * dst , const void * src , size_t n )
  { (void) DeepCopy< HostSpace , ResCudaSpace , Cuda >( dst , src , n ); }

  inline
  DeepCopy( const ExecutionSpace& exec, void * dst , const void * src , size_t n )
  {
    exec.fence();
    DeepCopyAsyncCuda (dst,src,n);
  }
};

template<class ExecutionSpace>
struct DeepCopy< ResCudaSpace , CudaSpace , ExecutionSpace >
{
  inline
  DeepCopy( void * dst , const void * src , size_t n )
  { (void) DeepCopy< CudaSpace , CudaSpace , Cuda >( dst , src , n ); }

  inline
  DeepCopy( const ExecutionSpace& exec, void * dst , const void * src , size_t n )
  {
    exec.fence();
    DeepCopyAsyncCuda (dst,src,n);
  }
};

template<class ExecutionSpace>
struct DeepCopy< CudaSpace , ResCudaSpace , ExecutionSpace>
{
  inline
  DeepCopy( void * dst , const void * src , size_t n )
  { (void) DeepCopy< CudaSpace , CudaSpace , Cuda >( dst , src , n ); }

  inline
  DeepCopy( const ExecutionSpace& exec, void * dst , const void * src , size_t n )
  {
    exec.fence();
    DeepCopyAsyncCuda (dst,src,n);
  }
};

} // namespace Impl
} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

/** Running in ResCudaSpace attempting to access HostSpace: error */
template<>
struct VerifyExecutionCanAccessMemorySpace< Kokkos::ResCudaSpace , Kokkos::HostSpace >
{
  enum { value = false };
  KOKKOS_INLINE_FUNCTION static void verify( void )
    { Kokkos::abort("Cuda code attempted to access HostSpace memory"); }

  KOKKOS_INLINE_FUNCTION static void verify( const void * )
    { Kokkos::abort("Cuda code attempted to access HostSpace memory"); }
};

/** Running in ResCudaSpace accessing CudaUVMSpace: ok */
template<>
struct VerifyExecutionCanAccessMemorySpace< Kokkos::ResCudaSpace , Kokkos::CudaUVMSpace >
{
  enum { value = true };
  KOKKOS_INLINE_FUNCTION static void verify( void ) { }
  KOKKOS_INLINE_FUNCTION static void verify( const void * ) { }
};

/** Running in CudaSpace accessing CudaHostPinnedSpace: ok */
template<>
struct VerifyExecutionCanAccessMemorySpace< Kokkos::ResCudaSpace , Kokkos::CudaHostPinnedSpace >
{
  enum { value = true };
  KOKKOS_INLINE_FUNCTION static void verify( void ) { }
  KOKKOS_INLINE_FUNCTION static void verify( const void * ) { }
};

/** Running in CudaSpace attempting to access an unknown space: error */
template< class OtherSpace >
struct VerifyExecutionCanAccessMemorySpace<
  typename enable_if< ! is_same<Kokkos::ResCudaSpace,OtherSpace>::value , Kokkos::ResCudaSpace >::type ,
  OtherSpace >
{
  enum { value = false };
  KOKKOS_INLINE_FUNCTION static void verify( void )
    { Kokkos::abort("Cuda code attempted to access unknown Space memory"); }

  KOKKOS_INLINE_FUNCTION static void verify( const void * )
    { Kokkos::abort("Cuda code attempted to access unknown Space memory"); }
};

//----------------------------------------------------------------------------
/** Running in HostSpace attempting to access CudaSpace */
template<>
struct VerifyExecutionCanAccessMemorySpace< Kokkos::HostSpace , Kokkos::ResCudaSpace >
{
  enum { value = false };
  inline static void verify( void ) { ResCudaSpace::access_error(); }
  inline static void verify( const void * p ) { ResCudaSpace::access_error(p); }
};

} // namespace Impl
} // namespace Kokkos

//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

namespace Kokkos {
namespace Impl {

template<>
class SharedAllocationRecord< Kokkos::ResCudaSpace , void >
  : public SharedAllocationRecord< void , void >
{
private:

  friend class SharedAllocationRecord< Kokkos::CudaUVMSpace , void > ;

  typedef SharedAllocationRecord< void , void >  RecordBase ;

  SharedAllocationRecord( const SharedAllocationRecord & ) = delete ;
  SharedAllocationRecord & operator = ( const SharedAllocationRecord & ) = delete ;

  static void deallocate( RecordBase * );

  static ::cudaTextureObject_t
  attach_texture_object( const unsigned sizeof_alias
                       , void * const   alloc_ptr
                       , const size_t   alloc_size );

#ifdef KOKKOS_DEBUG
  static RecordBase s_root_record ;
#endif

  ::cudaTextureObject_t   m_tex_obj ;
  const Kokkos::ResCudaSpace m_space ;

protected:

  ~SharedAllocationRecord();
  SharedAllocationRecord() : RecordBase(), m_tex_obj(0), m_space() {}

  SharedAllocationRecord( const Kokkos::ResCudaSpace        & arg_space
                        , const std::string              & arg_label
                        , const size_t                     arg_alloc_size
                        , const RecordBase::function_type  arg_dealloc = & deallocate
                        );

public:
  Kokkos::ResCudaSpace get_space() const { return m_space; }
  std::string get_label() const ;

  static SharedAllocationRecord * allocate( const Kokkos::ResCudaSpace &  arg_space
                                          , const std::string       &  arg_label
                                          , const size_t               arg_alloc_size );

  /**\brief  Allocate tracked memory in the space */
  static
  void * allocate_tracked( const Kokkos::ResCudaSpace & arg_space
                         , const std::string & arg_label
                         , const size_t arg_alloc_size );

  /**\brief  Reallocate tracked memory in the space */
  static
  void * reallocate_tracked( void * const arg_alloc_ptr
                           , const size_t arg_alloc_size );

  /**\brief  Deallocate tracked memory in the space */
  static
  void deallocate_tracked( void * const arg_alloc_ptr );

  static SharedAllocationRecord * get_record( void * arg_alloc_ptr );

  template< typename AliasType >
  inline
  ::cudaTextureObject_t attach_texture_object()
    {
      static_assert( ( std::is_same< AliasType , int >::value ||
                       std::is_same< AliasType , ::int2 >::value ||
                       std::is_same< AliasType , ::int4 >::value )
                   , "Cuda texture fetch only supported for alias types of int, ::int2, or ::int4" );

      if ( m_tex_obj == 0 ) {
        m_tex_obj = attach_texture_object( sizeof(AliasType)
                                         , (void*) RecordBase::m_alloc_ptr
                                         , RecordBase::m_alloc_size );
      }

      return m_tex_obj ;
    }

  template< typename AliasType >
  inline
  int attach_texture_object_offset( const AliasType * const ptr )
    {
      // Texture object is attached to the entire allocation range
      return ptr - reinterpret_cast<AliasType*>( RecordBase::m_alloc_ptr );
    }

  static void print_records( std::ostream & , const Kokkos::ResCudaSpace & , bool detail = false );
};



} // namespace Impl

namespace Experimental {
//  template <class DType, class ExecSpace> void * CombineFunctor<DType,ExecSpace>::s_dup_kernel = nullptr;

  template<class Functor>
  __global__ static void launch_comb_dup_kernel( const Functor cf ) {
       int iwork = threadIdx.y + blockDim.y * blockIdx.x ;
       if ( iwork <  cf.get_len() )
          cf.exec(iwork);
  }
/*
   template __global__ static void launch_comb_dup_kernel<CombineFunctor<int, Kokkos::ResCuda> >(
                                                                        CombineFunctor<int, Kokkos::ResCuda> );

   template<> void * CombineFunctor<int, Kokkos::ResCuda>::s_dup_kernel = (void*)&launch_comb_dup_kernel< CombineFunctor<int, Kokkos::ResCuda> >;
*/
   static void (*pLaunch)(const CombineFunctor<int, Kokkos::ResCuda>) = &launch_comb_dup_kernel<CombineFunctor<int, Kokkos::ResCuda> >; 
 
  template< class Type, class MemorySpace >
  static void track_duplicate( Kokkos::Impl::SharedAllocationRecord<void,void> * orig, Kokkos::Impl::SharedAllocationRecord<void,void> * dup ) {
    Kokkos::Impl::SharedAllocationRecord<MemorySpace,void> * SP = static_cast<Kokkos::Impl::SharedAllocationRecord<MemorySpace,void> *>(dup);
    typedef Kokkos::Experimental::SpecDuplicateTracker<Type, typename MemorySpace::execution_space> dt_type;
    typedef CombineFunctor<typename dt_type::rd_type, typename MemorySpace::execution_space> comb_type;

    dt_type * dt = nullptr;
    auto loc = MemorySpace::duplicate_map.find(SP->get_label());
    if ( loc != MemorySpace::duplicate_map.end() ) {
       dt = (dt_type*)loc->second;
       printf("retrieved existing tracking entry from map: %s\n", SP->get_label().c_str());
    } else {
       dt = new dt_type();
       //printf("dup_kernel ptr = %08x \n", comb_type::s_dup_kernel); 
       dt->func_ptr = (void*)pLaunch; // comb_type::s_dup_kernel;
       dt->data_len = orig->size();
       dt->original_data = orig->data();
       MemorySpace::duplicate_map[SP->get_label()] = static_cast<Kokkos::Experimental::DuplicateTracker*>(dt);
       printf("creating new tracking entry in hash map: %s, %08x \n", SP->get_label().c_str(), (unsigned long)dt->func_ptr);
    }
    dt->add_dup(dup);
 }
} // namespace Experimental

} // namespace Kokkos

/*
#define KOKKOS_DECLARE_RESILIENCE_OBJECTS(data_type) \
   static void (*Kokkos::Experimental::pLaunch)(const Kokkos:Experimental::CombineFunctor<data_type, Kokkos::ResCuda>) = \
                        &Kokkos::Experimental::launch_comb_dup_kernel<Kokkos::Experimental::CombineFunctor<data_type, Kokkos::ResCuda> >; 
*/
//----------------------------------------------------------------------------
//----------------------------------------------------------------------------

#endif /* #if defined( KOKKOS_ENABLE_CUDA ) */
#endif /* #define KOKKOS_RESCUDASPACE_HPP */

