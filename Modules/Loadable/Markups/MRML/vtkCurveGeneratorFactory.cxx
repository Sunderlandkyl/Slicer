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
  and was supported through CANARIE's Research Software Program, Cancer
  Care Ontario, OpenAnatomy, and Brigham and Women's Hospital through NIH grant R01MH112748.

==============================================================================*/

// vtkAddon includes
#include "vtkCurveGeneratorFactory.h"

// VTK includes
#include <vtkObjectFactory.h>
#include <vtkDataObject.h>

// STD includes
#include <algorithm>

//----------------------------------------------------------------------------
// The compression curveGenerator manager singleton.
// This MUST be default initialized to zero by the compiler and is
// therefore not initialized here.  The ClassInitialize and ClassFinalize methods handle this instance.
static vtkCurveGeneratorFactory* vtkCurveGeneratorFactoryInstance;


//----------------------------------------------------------------------------
// Must NOT be initialized.  Default initialization to zero is necessary.
unsigned int vtkCurveGeneratorFactoryInitialize::Count;

//----------------------------------------------------------------------------
// Implementation of vtkCurveGeneratorFactoryInitialize class.
//----------------------------------------------------------------------------
vtkCurveGeneratorFactoryInitialize::vtkCurveGeneratorFactoryInitialize()
{
  if (++Self::Count == 1)
    {
    vtkCurveGeneratorFactory::classInitialize();
    }
}

//----------------------------------------------------------------------------
vtkCurveGeneratorFactoryInitialize::~vtkCurveGeneratorFactoryInitialize()
{
  if (--Self::Count == 0)
    {
    vtkCurveGeneratorFactory::classFinalize();
    }
}

//----------------------------------------------------------------------------


//----------------------------------------------------------------------------
// Up the reference count so it behaves like New
vtkCurveGeneratorFactory* vtkCurveGeneratorFactory::New()
{
  vtkCurveGeneratorFactory* ret = vtkCurveGeneratorFactory::GetInstance();
  ret->Register(nullptr);
  ret->RegisterCurveGenerator(vtkNew<vtkCurveGenerator>());
  return ret;
}

//----------------------------------------------------------------------------
// Return the single instance of the vtkCurveGeneratorFactory
vtkCurveGeneratorFactory* vtkCurveGeneratorFactory::GetInstance()
{
  if (!vtkCurveGeneratorFactoryInstance)
    {
    // Try the factory first
    vtkCurveGeneratorFactoryInstance = (vtkCurveGeneratorFactory*)vtkObjectFactory::CreateInstance("vtkCurveGeneratorFactory");
    // if the factory did not provide one, then create it here
    if (!vtkCurveGeneratorFactoryInstance)
      {
      vtkCurveGeneratorFactoryInstance = new vtkCurveGeneratorFactory;
#ifdef VTK_HAS_INITIALIZE_OBJECT_BASE
      vtkCurveGeneratorFactoryInstance->InitializeObjectBase();
#endif
      }
    }
  // return the instance
  return vtkCurveGeneratorFactoryInstance;
}

//----------------------------------------------------------------------------
vtkCurveGeneratorFactory::vtkCurveGeneratorFactory()
= default;

//----------------------------------------------------------------------------
vtkCurveGeneratorFactory::~vtkCurveGeneratorFactory()
= default;

//----------------------------------------------------------------------------
void vtkCurveGeneratorFactory::PrintSelf(ostream & os, vtkIndent indent)
{
  this->vtkObject::PrintSelf(os, indent);
}

//----------------------------------------------------------------------------
void vtkCurveGeneratorFactory::classInitialize()
{
  // Allocate the singleton
  vtkCurveGeneratorFactoryInstance = vtkCurveGeneratorFactory::GetInstance();
}

//----------------------------------------------------------------------------
void vtkCurveGeneratorFactory::classFinalize()
{
  vtkCurveGeneratorFactoryInstance->Delete();
  vtkCurveGeneratorFactoryInstance = nullptr;
}

//----------------------------------------------------------------------------
bool vtkCurveGeneratorFactory::RegisterCurveGenerator(vtkCurveGenerator* curveGenerator)
{
  for (unsigned int i = 0; i < this->RegisteredCurveGenerators.size(); ++i)
    {
    if (strcmp(this->RegisteredCurveGenerators[i]->GetClassName(), curveGenerator->GetClassName()) == 0)
      {
      vtkWarningMacro("RegisterCurveGenerator failed: curveGenerator is already registered");
      return false;
      }
    }
  vtkSmartPointer<vtkCurveGenerator> curveGeneratorPointer = vtkSmartPointer<vtkCurveGenerator>(curveGenerator);
  this->RegisteredCurveGenerators.push_back(curveGeneratorPointer);
  return true;
}

//----------------------------------------------------------------------------
bool vtkCurveGeneratorFactory::UnRegisterStreamingCurveGeneratorByClassName(const std::string & curveGeneratorClassName)
{
  std::vector<vtkSmartPointer<vtkCurveGenerator> >::iterator curveGeneratorIt;
  for (curveGeneratorIt = this->RegisteredCurveGenerators.begin(); curveGeneratorIt != this->RegisteredCurveGenerators.end(); ++curveGeneratorIt)
    {
    vtkSmartPointer<vtkCurveGenerator> curveGenerator = *curveGeneratorIt;
    if (strcmp(curveGenerator->GetClassName(), curveGeneratorClassName.c_str()) == 0)
      {
      this->RegisteredCurveGenerators.erase(curveGeneratorIt);
      return true;
      }
    }
  vtkWarningMacro("UnRegisterStreamingCurveGeneratorByClassName failed: curveGenerator not found");
  return false;
}

//----------------------------------------------------------------------------
vtkCurveGenerator* vtkCurveGeneratorFactory::CreateCurveGeneratorByClassName(const std::string & curveGeneratorClassName)
{
  std::vector<vtkSmartPointer<vtkCurveGenerator> >::iterator curveGeneratorIt;
  for (curveGeneratorIt = this->RegisteredCurveGenerators.begin(); curveGeneratorIt != this->RegisteredCurveGenerators.end(); ++curveGeneratorIt)
    {
    vtkSmartPointer<vtkCurveGenerator> curveGenerator = *curveGeneratorIt;
    if (strcmp(curveGenerator->GetClassName(), curveGeneratorClassName.c_str()) == 0)
      {
      return curveGenerator->CreateInstance();
      }
    }
  return nullptr;
}

//----------------------------------------------------------------------------
const std::vector<std::string> vtkCurveGeneratorFactory::GetStreamingCurveGeneratorClassNames()
{
  std::vector<std::string> curveGeneratorClassNames;
  std::vector<vtkSmartPointer<vtkCurveGenerator> >::iterator curveGeneratorIt;
  for (curveGeneratorIt = this->RegisteredCurveGenerators.begin(); curveGeneratorIt != this->RegisteredCurveGenerators.end(); ++curveGeneratorIt)
  {
    vtkSmartPointer<vtkCurveGenerator> curveGenerator = *curveGeneratorIt;
    curveGeneratorClassNames.emplace_back(curveGenerator->GetClassName());
  }
  return curveGeneratorClassNames;
}
