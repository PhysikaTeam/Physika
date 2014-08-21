/*
 * @file mpm_solid_base.cpp 
 * @Brief base class of all MPM drivers for solid.
 * @author Fei Zhu
 * 
 * This file is part of Physika, a versatile physics simulation library.
 * Copyright (C) 2013 Physika Group.
 *
 * This Source Code Form is subject to the terms of the GNU General Public License v2.0. 
 * If a copy of the GPL was not distributed with this file, you can obtain one at:
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 */

#include <cstdlib>
#include <iostream>
#include <algorithm>
#include "Physika_Core/Utilities/math_utilities.h"
#include "Physika_Core/Vectors/vector_2d.h"
#include "Physika_Core/Vectors/vector_3d.h"
#include "Physika_Dynamics/Particles/solid_particle.h"
#include "Physika_Dynamics/MPM/mpm_solid_base.h"
#include "Physika_Dynamics/MPM/MPM_Step_Methods/mpm_solid_step_method_USL.h"

namespace Physika{

template <typename Scalar, int Dim>
MPMSolidBase<Scalar,Dim>::MPMSolidBase()
    :MPMBase<Scalar,Dim>()
{
    this->template setStepMethod<MPMSolidStepMethodUSL<Scalar,Dim> >(); //default step method is USL
}

template <typename Scalar, int Dim>
MPMSolidBase<Scalar,Dim>::MPMSolidBase(unsigned int start_frame, unsigned int end_frame, Scalar frame_rate, Scalar max_dt, bool write_to_file)
    :MPMBase<Scalar,Dim>(start_frame,end_frame,frame_rate,max_dt,write_to_file)
{
    this->template setStepMethod<MPMSolidStepMethodUSL<Scalar,Dim> >(); //default step method is USL
}

template <typename Scalar, int Dim>
MPMSolidBase<Scalar,Dim>::MPMSolidBase(unsigned int start_frame, unsigned int end_frame, Scalar frame_rate, Scalar max_dt, bool write_to_file,
                               const std::vector<SolidParticle<Scalar,Dim>*> &particles)
    :MPMBase<Scalar,Dim>(start_frame,end_frame,frame_rate,max_dt,write_to_file)
{
    setParticles(particles);
}

template <typename Scalar, int Dim>
MPMSolidBase<Scalar,Dim>::~MPMSolidBase()
{
    for(unsigned int i = 0; i < particles_.size(); ++i)
        if(particles_[i])
            delete particles_[i];
}

template <typename Scalar, int Dim>
unsigned int MPMSolidBase<Scalar,Dim>::particleNum() const
{
    return particles_.size();
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::addParticle(const SolidParticle<Scalar,Dim> &particle)
{
    SolidParticle<Scalar,Dim> *new_particle = particle.clone();
    particles_.push_back(new_particle);
    //add space for particle related data
    unsigned char not_boundary = 0;
    is_bc_particle_.push_back(not_boundary);
    particle_initial_volume_.push_back(new_particle->volume()); //store particle initial volume
    //for each particle, preallocate space that can store weight/gradient of maximum
    //number of nodes in rangec
    appendSpaceForWeightAndGradient();
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::removeParticle(unsigned int particle_idx)
{
    if(particle_idx>=particles_.size())
    {
        std::cerr<<"Warning: MPM particle index out of range, operation ignored!\n";
        return;
    }
    typename std::vector<SolidParticle<Scalar,Dim>*>::iterator iter = particles_.begin() + particle_idx;
    delete *iter; //release memory
    particles_.erase(iter);
    //remove the record in particle related data
    typename std::vector<Scalar>::iterator iter2 = particle_initial_volume_.begin() + particle_idx;
    particle_initial_volume_.erase(iter2);
    typename std::vector<unsigned char>::iterator iter3 = is_bc_particle_.begin() + particle_idx;
    is_bc_particle_.erase(iter3);
    typename std::vector<std::vector<MPMInternal::NodeIndexWeightGradientPair<Scalar,Dim> > >::iterator iter4 = this->particle_grid_weight_and_gradient_.begin() + particle_idx;
    this->particle_grid_weight_and_gradient_.erase(iter4);
    typename std::vector<unsigned int>::iterator iter5 = this->particle_grid_pair_num_.begin() + particle_idx;
    this->particle_grid_pair_num_.erase(iter5);
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::setParticles(const std::vector<SolidParticle<Scalar,Dim>*> &particles)
{
    //release data first
    for(unsigned int i = 0; i < particles_.size(); ++i)
        if(particles_[i])
            delete particles_[i];
    particles_.resize(particles.size());
    is_bc_particle_.resize(particles.size());
    particle_initial_volume_.resize(particles.size());
    //for each particle, preallocate space that can store weight/gradient of maximum
    //number of nodes in range
    allocateSpaceForWeightAndGradient();
    //add new particle data
    for(unsigned int i = 0; i < particles.size(); ++i)
    {
        if(particles[i]==NULL)
        {
            std::cerr<<"Warning: pointer to particle "<<i<<" is NULL, ignored!\n";
            continue;
        } 
        particles_[i] = particles[i]->clone();
        is_bc_particle_[i] = 0;
        particle_initial_volume_[i] = particles_[i]->volume();
    }
}

template <typename Scalar, int Dim>
const SolidParticle<Scalar,Dim>& MPMSolidBase<Scalar,Dim>::particle(unsigned int particle_idx) const
{
    if(particle_idx>=particles_.size())
    {
        std::cerr<<"Error: MPM particle index out of range, abort program!\n";
        std::exit(EXIT_FAILURE);
    }
    return *particles_[particle_idx];
}

template <typename Scalar, int Dim>
SolidParticle<Scalar,Dim>& MPMSolidBase<Scalar,Dim>::particle(unsigned int particle_idx)
{
    if(particle_idx>=particles_.size())
    {
        std::cerr<<"Error: MPM particle index out of range, abort program!\n";
        std::exit(EXIT_FAILURE);
    }
    return *particles_[particle_idx];
}

template <typename Scalar, int Dim>
const std::vector<SolidParticle<Scalar,Dim>*>& MPMSolidBase<Scalar,Dim>::allParticles() const
{
    return particles_;
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::addBCParticle(unsigned int particle_idx)
{
    if(particle_idx>=particles_.size())
    {
        std::cerr<<"Warning: MPM particle index out of range, operation ignored!\n";
        return;
    }
    is_bc_particle_[particle_idx] = 1;
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::addBCParticles(const std::vector<unsigned int> &particle_idx)
{
    for(unsigned int i = 0; i < particle_idx.size(); ++i)
        addBCParticle(particle_idx[i]);
}

template <typename Scalar, int Dim>
Scalar MPMSolidBase<Scalar,Dim>::maxParticleVelocityNorm() const
{
    if(particles_.empty())
        return 0;
    Scalar min_vel = (std::numeric_limits<Scalar>::max)();
    for(unsigned int i = 0; i < particles_.size(); ++i)
    {
        Scalar norm_sqr = (particles_[i]->velocity()).normSquared();
        min_vel = norm_sqr < min_vel ? norm_sqr : min_vel;
    }
    return sqrt(min_vel);
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::allocateSpaceForWeightAndGradient()
{
    PHYSIKA_ASSERT(this->weight_function_);
    //for each particle, preallocate space that can store weight/gradient of maximum
    //number of nodes in range
    unsigned int max_num = 1;
    for(unsigned int i = 0; i < Dim; ++i)
        max_num *= (this->weight_function_->supportRadius())*2+1;
    std::vector<MPMInternal::NodeIndexWeightGradientPair<Scalar,Dim> > max_num_weight_and_gradient_vec(max_num);
    this->particle_grid_weight_and_gradient_.resize(particles_.size(),max_num_weight_and_gradient_vec);
    this->particle_grid_pair_num_.resize(particles_.size(),0);
}

template <typename Scalar, int Dim>
void MPMSolidBase<Scalar,Dim>::appendSpaceForWeightAndGradient()
{
    unsigned int max_num = 1;
    for(unsigned int i = 0; i < Dim; ++i)
        max_num *= (this->weight_function_->supportRadius())*2+1;
    std::vector<MPMInternal::NodeIndexWeightGradientPair<Scalar,Dim> > max_num_weight_and_gradient_vec(max_num);
    this->particle_grid_weight_and_gradient_.push_back(max_num_weight_and_gradient_vec);
    this->particle_grid_pair_num_.push_back(0);
}

//explicit instantiations
template class MPMSolidBase<float,2>;
template class MPMSolidBase<float,3>;
template class MPMSolidBase<double,2>;
template class MPMSolidBase<double,3>;

}  //end of namespace Physika