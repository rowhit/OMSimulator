/*
 * This file is part of OpenModelica.
 *
 * Copyright (c) 1998-CurrentYear, Open Source Modelica Consortium (OSMC),
 * c/o Linköpings universitet, Department of Computer and Information Science,
 * SE-58183 Linköping, Sweden.
 *
 * All rights reserved.
 *
 * THIS PROGRAM IS PROVIDED UNDER THE TERMS OF GPL VERSION 3 LICENSE OR
 * THIS OSMC PUBLIC LICENSE (OSMC-PL) VERSION 1.2.
 * ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS PROGRAM CONSTITUTES
 * RECIPIENT'S ACCEPTANCE OF THE OSMC PUBLIC LICENSE OR THE GPL VERSION 3,
 * ACCORDING TO RECIPIENTS CHOICE.
 *
 * The OpenModelica software and the Open Source Modelica
 * Consortium (OSMC) Public License (OSMC-PL) are obtained
 * from OSMC, either from the above address,
 * from the URLs: http://www.ida.liu.se/projects/OpenModelica or
 * http://www.openmodelica.org, and in the OpenModelica distribution.
 * GNU version 3 is obtained from: http://www.gnu.org/copyleft/gpl.html.
 *
 * This program is distributed WITHOUT ANY WARRANTY; without
 * even the implied warranty of  MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE, EXCEPT AS EXPRESSLY SET FORTH
 * IN THE BY RECIPIENT SELECTED SUBSIDIARY LICENSE CONDITIONS OF OSMC-PL.
 *
 * See the full OSMC Public License conditions for more details.
 *
 */

#include "SystemWC.h"

#include "Component.h"
#include "ComponentFMUCS.h"
#include "Flags.h"
#include "Model.h"
#include "ssd/Tags.h"
#include "SystemTLM.h"
#include "Types.h"

oms3::SystemWC::SystemWC(const ComRef& cref, Model* parentModel, System* parentSystem)
  : oms3::System(cref, oms_system_wc, parentModel, parentSystem)
{
}

oms3::SystemWC::~SystemWC()
{
}

oms3::System* oms3::SystemWC::NewSystem(const oms3::ComRef& cref, oms3::Model* parentModel, oms3::System* parentSystem)
{
  if (!cref.isValidIdent())
  {
    logError_InvalidIdent(cref);
    return NULL;
  }

  if ((parentModel && parentSystem) || (!parentModel && !parentSystem))
  {
    logError_InternalError;
    return NULL;
  }

  System* system = new SystemWC(cref, parentModel, parentSystem);
  return system;
}

oms_status_enu_t oms3::SystemWC::exportToSSD_SimulationInformation(pugi::xml_node& node) const
{
  pugi::xml_node node_simulation_information = node.append_child(oms2::ssd::ssd_simulation_information);

  pugi::xml_node node_solver = node_simulation_information.append_child("FixedStepMaster");
  node_solver.append_attribute("description") = solverName.c_str();
  node_solver.append_attribute("stepSize") = std::to_string(stepSize).c_str();

  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::importFromSSD_SimulationInformation(const pugi::xml_node& node)
{
  solverName = node.child("FixedStepMaster").attribute("description").as_string();
  stepSize = node.child("FixedStepMaster").attribute("stepSize").as_double();
  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::instantiate()
{
  for (const auto& subsystem : getSubSystems())
    if (oms_status_ok != subsystem.second->instantiate())
      return oms_status_error;

  for (const auto& component : getComponents())
    if (oms_status_ok != component.second->instantiate())
      return oms_status_error;

  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::initialize()
{
  clock.reset();
  CallClock callClock(clock);

  time = getModel()->getStartTime();

  for (const auto& subsystem : getSubSystems())
    if (oms_status_ok != subsystem.second->initialize())
      return oms_status_error;

  for (const auto& component : getComponents())
    if (oms_status_ok != component.second->initialize())
      return oms_status_error;

  if (oms_status_ok != updateDependencyGraphs())
    return oms_status_error;

  if (oms_status_ok != updateInputs(initialUnknownsGraph))
    return oms_status_error;

  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::terminate()
{
  for (const auto& subsystem : getSubSystems())
    if (oms_status_ok != subsystem.second->terminate())
      return oms_status_error;

  for (const auto& component : getComponents())
    if (oms_status_ok != component.second->terminate())
      return oms_status_error;

  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::reset()
{
  for (const auto& subsystem : getSubSystems())
    if (oms_status_ok != subsystem.second->reset())
      return oms_status_error;

  for (const auto& component : getComponents())
    if (oms_status_ok != component.second->reset())
      return oms_status_error;

  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::stepUntil(double stopTime, void (*cb)(const char* ident, double time, oms_status_enu_t status))
{
  CallClock callClock(clock);
  ComRef modelName = this->getModel()->getCref();

  double startTime=time;

  if (Flags::ProgressBar())
    logInfo("stepUntil [" + std::to_string(startTime) + "; " + std::to_string(stopTime) + "]");

  while (time < stopTime)
  {
    double tNext = time+stepSize;
    if (tNext > stopTime)
      tNext = stopTime;

    logDebug("doStep: " + std::to_string(time) + " -> " + std::to_string(tNext));

    oms_status_enu_t status;
    for (const auto& subsystem : getSubSystems())
    {
      status = subsystem.second->stepUntil(tNext, NULL);
      if (oms_status_ok != status)
      {
        if (cb)
          cb(modelName.c_str(), tNext, status);
        return status;
      }
    }

    for (const auto& component : getComponents())
    {
      status = component.second->stepUntil(tNext);
      if (oms_status_ok != status)
      {
        if (cb)
          cb(modelName.c_str(), tNext, status);
        return status;
      }
    }

    time = tNext;
    if (isTopLevelSystem())
      getModel()->emit(time);
    updateInputs(outputsGraph);
    if (isTopLevelSystem())
      getModel()->emit(time);

    if (cb)
      cb(modelName.c_str(), time, oms_status_ok);

    if (Flags::ProgressBar())
      Log::ProgressBar(startTime, stopTime, time);

    if (isTopLevelSystem() && getModel()->cancelSimulation())
    {
      cb(modelName.c_str(), time, oms_status_discard);
      return oms_status_discard;
    }
  }

  if (Flags::ProgressBar())
  {
    Log::ProgressBar(startTime, stopTime, time);
    Log::TerminateBar();
  }
  return oms_status_ok;
}

oms_status_enu_t oms3::SystemWC::updateInputs(oms3::DirectedGraph& graph)
{
  CallClock callClock(clock);

  // input := output
  const std::vector< std::vector< std::pair<int, int> > >& sortedConnections = graph.getSortedConnections();
  for(int i=0; i<sortedConnections.size(); i++)
  {
    if (sortedConnections[i].size() == 1)
    {
      int output = sortedConnections[i][0].first;
      int input = sortedConnections[i][0].second;

      if (graph.getNodes()[input].getType() == oms_signal_type_real)
      {
        double value = 0.0;
        if (oms_status_ok != getReal(graph.getNodes()[output].getName(), value)) return oms_status_error;
        if (oms_status_ok != setReal(graph.getNodes()[input].getName(), value)) return oms_status_error;
      }
      else if (graph.getNodes()[input].getType() == oms_signal_type_integer)
      {
        int value = 0.0;
        if (oms_status_ok != getInteger(graph.getNodes()[output].getName(), value)) return oms_status_error;
        if (oms_status_ok != setInteger(graph.getNodes()[input].getName(), value)) return oms_status_error;
      }
      else if (graph.getNodes()[input].getType() == oms_signal_type_boolean)
      {
        bool value = 0.0;
        if (oms_status_ok != getBoolean(graph.getNodes()[output].getName(), value)) return oms_status_error;
        if (oms_status_ok != setBoolean(graph.getNodes()[input].getName(), value)) return oms_status_error;
      }
      else
        return logError_InternalError;
    }
    else
    {
      if (oms_status_ok != solveAlgLoop(graph, sortedConnections[i])) return oms_status_error;
    }
  }
  return oms_status_ok;
}


oms_status_enu_t oms3::SystemWC::solveAlgLoop(DirectedGraph& graph, const std::vector< std::pair<int, int> >& SCC)
{
  CallClock callClock(clock);

  const int size = SCC.size();
  const int maxIterations = 10;
  double maxRes;
  double *res = new double[size]();

  int it=0;
  do
  {
    it++;
    // get old values
    for (int i=0; i<size; ++i)
    {
      int output = SCC[i].first;
      if (oms_status_ok != getReal(graph.getNodes()[output].getName(), res[i]))
      {
        delete[] res;
        return oms_status_error;
      }
    }

    // update inputs
    for (int i=0; i<size; ++i)
    {
      int input = SCC[i].second;
      if (oms_status_ok != setReal(graph.getNodes()[input].getName(), res[i]))
      {
        delete[] res;
        return oms_status_error;
      }
    }

    // calculate residuals
    maxRes = 0.0;
    double value;
    for (int i=0; i<size; ++i)
    {
      int output = SCC[i].first;
      if (oms_status_ok != getReal(graph.getNodes()[output].getName(), value))
      {
        delete[] res;
        return oms_status_error;
      }
      res[i] -= value;

      if (fabs(res[i]) > maxRes)
        maxRes = fabs(res[i]);
    }
  } while(maxRes > tolerance && it < maxIterations);

  delete[] res;

  if (it >= maxIterations)
    return logError("max. number of iterations (" + std::to_string(maxIterations) + ") exceeded at time = " + std::to_string(getTime()));
  logDebug("CompositeModel::solveAlgLoop: maxRes: " + std::to_string(maxRes) + ", iterations: " + std::to_string(it) + " at time = " + std::to_string(getTime()));
  return oms_status_ok;
}
