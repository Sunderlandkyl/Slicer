/*==============================================================================

Copyright (c) Laboratory for Percutaneous Surgery (PerkLab)
Queen's University, Kingston, ON, Canada. All Rights Reserved.

See COPYRIGHT.txt
or http://www.slicer.org/copyright/copyright.txt for details.

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

This file was originally developed by Kyle Sunderland, PerkLab, Queen's University
and was supported through CANARIE's Research Software Program, and Cancer
Care Ontario.

==============================================================================*/

#ifndef __vtkCurveGeneratorFactory_h
#define __vtkCurveGeneratorFactory_h

// VTK includes
#include <vtkObject.h>
#include <vtkSmartPointer.h>

// STD includes
#include <map>

// vtkAddon includes
#include "vtkAddon.h"
#include "vtkCurveGenerator.h"

#include "vtkSlicerMarkupsModuleMRMLExport.h"

/// \ingroup Volumes
/// \brief Class that can create compresion device for streaming volume instances.
///
/// This singleton class is a repository of all compression curveGenerators for compressing volume.
/// Singleton pattern adopted from vtkEventBroker class.
class VTK_SLICER_MARKUPS_MODULE_MRML_EXPORT vtkCurveGeneratorFactory : public vtkObject
{
public:

  vtkTypeMacro(vtkCurveGeneratorFactory, vtkObject);
  void PrintSelf(ostream& os, vtkIndent indent) override;

  /// Registers a new curve generator
  /// \param curveGenerator Pointer to an instance of the curve generator
  bool RegisterCurveGenerator(vtkCurveGenerator* curveGenerator);

  /// Removes a curve generator from the factory
  /// This does not affect curve generators that have already been instantiated
  /// \param curveGeneratorClassName class name of the curve generator class that is being unregistered
  /// Returns true if the curve generator is successfully unregistered
  bool UnRegisterStreamingCurveGeneratorByClassName(const std::string& curveGeneratorClassName);

  /// Instantiate and return a new curve generatorm or nullptr if the requested curve generator could not be found
  /// Usage: vtkSmartPointer<vtkCurveGenerator> curveGenerator = CreateCurveGeneratorByClassName("vtkCurveGenerator");
  /// Returns nullptr if no matching curveGenerator can be found
  vtkCurveGenerator* CreateCurveGeneratorByClassName(const std::string& curveGeneratorClassName);

  /// Returns a list of all registered curve generators
  const std::vector<std::string> GetStreamingCurveGeneratorClassNames();

public:
  /// Return the singleton instance with no reference counting.
  static vtkCurveGeneratorFactory* GetInstance();

  /// This is a singleton pattern New.  There will only be ONE
  /// reference to a vtkCurveGeneratorFactory object per process.  Clients that
  /// call this must call Delete on the object so that the reference
  /// counting will work. The single instance will be unreferenced when
  /// the program exits.
  static vtkCurveGeneratorFactory* New();

protected:
  vtkCurveGeneratorFactory();
  ~vtkCurveGeneratorFactory() override;
  vtkCurveGeneratorFactory(const vtkCurveGeneratorFactory&);
  void operator=(const vtkCurveGeneratorFactory&);

  friend class vtkCurveGeneratorFactoryInitialize;
  typedef vtkCurveGeneratorFactory Self;

  // Singleton management functions.
  static void classInitialize();
  static void classFinalize();

  /// Registered curveGenerator classes
  std::vector< vtkSmartPointer<vtkCurveGenerator> > RegisteredCurveGenerators;
};


/// Utility class to make sure qSlicerModuleManager is initialized before it is used.
class VTK_SLICER_MARKUPS_MODULE_MRML_EXPORT vtkCurveGeneratorFactoryInitialize
{
public:
  typedef vtkCurveGeneratorFactoryInitialize Self;

  vtkCurveGeneratorFactoryInitialize();
  ~vtkCurveGeneratorFactoryInitialize();
private:
  static unsigned int Count;
};

/// This instance will show up in any translation unit that uses
/// vtkCurveGeneratorFactory.  It will make sure vtkCurveGeneratorFactory is initialized
/// before it is used.
static vtkCurveGeneratorFactoryInitialize vtkCurveGeneratorFactoryInitializer;

#endif
