/*
 *  ac_generator.cpp
 *
 *  This file is part of NEST.
 *
 *  Copyright (C) 2004 The NEST Initiative
 *
 *  NEST is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  NEST is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with NEST.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <cmath>

#include "ac_generator.h"
#include "network.h"
#include "dict.h"
#include "integerdatum.h"
#include "doubledatum.h"
#include "dictutils.h"
#include "numerics.h"
#include "universal_data_logger_impl.h"   // issue #658

/* ----------------------------------------------------------------
 * block added for issue #658
 * ---------------------------------------------------------------- */

namespace nest
{
RecordablesMap< ac_generator > ac_generator::recordablesMap_;

template <>
void
RecordablesMap< ac_generator >::create()
{
  insert_( Name( names::amplitude ), &ac_generator::get_inj_ );
}
}

/* ----------------------------------------------------------------
 * Default constructors defining default parameters and state
 * ---------------------------------------------------------------- */

nest::ac_generator::Parameters_::Parameters_()
  : amp_( 0.0 )     // pA
  , offset_( 0.0 )  // pA
  , freq_( 0.0 )    // Hz
  , phi_deg_( 0.0 ) // degree
{
}

// method added for issue #658
nest::ac_generator::Parameters_::Parameters_( const Parameters_& p )
  : amp_( p.amp_ )
  , offset_( p.offset_ )
  , freq_( p.freq_ )
  , phi_deg_( p.phi_deg_ )
{
}

// method added for issue #658
nest::ac_generator::Parameters_& nest::ac_generator::Parameters_::
operator=( const Parameters_& p )
{
  if ( this == &p )
    return *this;

  amp_ = p.amp_;
  offset_ = p.offset_;
  freq_ = p.freq_;
  phi_deg_ = p.phi_deg_;

  return *this;
}

nest::ac_generator::State_::State_()
  : y_0_( 0.0 )
  , y_1_( 0.0 ) // pA
  , inj_( 0.0 ) // pA     // issue #658
{
}

// method added for issue #658
nest::ac_generator::Buffers_::Buffers_( ac_generator& n )
  : logger_( n )
{
}

// method added for issue #658
nest::ac_generator::Buffers_::Buffers_( const Buffers_&,
  ac_generator& n )
  : logger_( n )
{
}

/* ----------------------------------------------------------------
 * Parameter extraction and manipulation functions
 * ---------------------------------------------------------------- */

void
nest::ac_generator::Parameters_::get( DictionaryDatum& d ) const
{
  ( *d )[ names::amplitude ] = amp_;
  ( *d )[ names::offset ] = offset_;
  ( *d )[ names::phase ] = phi_deg_;
  ( *d )[ names::frequency ] = freq_;
}

void
nest::ac_generator::State_::get( DictionaryDatum& d ) const
{
  ( *d )[ "y_0" ] = y_0_;
  ( *d )[ "y_1" ] = y_1_;
}

void
nest::ac_generator::Parameters_::set( const DictionaryDatum& d )
{
  updateValue< double_t >( d, names::amplitude, amp_ );
  updateValue< double_t >( d, names::offset, offset_ );
  updateValue< double_t >( d, names::frequency, freq_ );
  updateValue< double_t >( d, names::phase, phi_deg_ );
}


/* ----------------------------------------------------------------
 * Default and copy constructor for node
 * ---------------------------------------------------------------- */

nest::ac_generator::ac_generator()
  : Node()
  , device_()
  , P_()
  , S_()
  , B_( *this )             // issue #658
{
  recordablesMap_.create(); // issue #658
}

nest::ac_generator::ac_generator( const ac_generator& n )
  : Node( n )
  , device_( n.device_ )
  , P_( n.P_ )
  , S_( n.S_ )
  , B_( n.B_, *this )       // issue #658
{
}


/* ----------------------------------------------------------------
 * Node initialization functions
 * ---------------------------------------------------------------- */

void
nest::ac_generator::init_state_( const Node& proto )
{
  const ac_generator& pr = downcast< ac_generator >( proto );

  device_.init_state( pr.device_ );
  S_ = pr.S_;
}

void
nest::ac_generator::init_buffers_()
{
  device_.init_buffers();
  B_.logger_.reset();           // issue #658
}

void
nest::ac_generator::calibrate()
{
  // issue #658 (below statement)
  B_.logger_.init(); // ensures initialization in case mm connected after Simulate

  device_.calibrate();

  const double_t h = Time::get_resolution().get_ms();
  const double_t t = network()->get_time().get_ms();

  // scale Hz to ms
  const double_t omega = 2.0 * numerics::pi * P_.freq_ / 1000.0;
  const double_t phi_rad = P_.phi_deg_ * 2.0 * numerics::pi / 360.0;

  // initial state
  S_.y_0_ = P_.amp_ * std::cos( omega * t + phi_rad );
  S_.y_1_ = P_.amp_ * std::sin( omega * t + phi_rad );

  // matrix elements
  V_.A_00_ = std::cos( omega * h );
  V_.A_01_ = -std::sin( omega * h );
  V_.A_10_ = std::sin( omega * h );
  V_.A_11_ = std::cos( omega * h );
}

void
nest::ac_generator::update( Time const& origin, const long_t from, const long_t to )
{
  assert( to >= 0 && ( delay ) from < Scheduler::get_min_delay() );   // issue #658
  assert( from < to );                                                // issue #658

  const long_t start = origin.get_steps();

  for ( long_t lag = from; lag < to; ++lag )
  {                                                                   // issue #658
    B_.logger_.record_data( origin.get_steps() + lag );               // issue #658
    S_.inj_ = 0.0;                                                    // issue #658

    if ( device_.is_active( Time::step( start + lag ) ) )
    {
      const double_t y_0 = S_.y_0_;
      S_.y_0_ = V_.A_00_ * y_0 + V_.A_01_ * S_.y_1_;
      S_.y_1_ = V_.A_10_ * y_0 + V_.A_11_ * S_.y_1_;
      S_.inj_ = S_.y_1_ + P_.offset_;                                 // issue #658

      CurrentEvent ce;    // issue #658 (moved to within if block; avoids unnecessary instantiation)
      ce.set_current( S_.y_1_ + P_.offset_ );
      network()->send( *this, ce, lag );
    }
  }                                                                   // issue #658
}

// method added for issue #658
void
nest::ac_generator::handle( DataLoggingRequest& e )
{
  B_.logger_.handle( e );
}
