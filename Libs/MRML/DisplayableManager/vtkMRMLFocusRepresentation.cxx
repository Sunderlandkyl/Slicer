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

// MRMLDM includes
#include "vtkMRMLFocusRepresentation.h"

// MRML includes
#include <vtkMRMLSelectionNode.h>

// VTK includes
#include <vtkActor2D.h>
#include <vtkOutlineGlowPass.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper2D.h>
#include <vtkRenderer.h>
#include <vtkRenderStepsPass.h>

class FocusDisplayPipeline
{
  public:

    //----------------------------------------------------------------------
    FocusDisplayPipeline()
    {
    }

    //----------------------------------------------------------------------
    virtual ~FocusDisplayPipeline()
    {
    }
};

//---------------------------------------------------------------------------
class vtkMRMLFocusRepresentation::vtkInternal
{
  public:
    vtkInternal(vtkMRMLFocusRepresentation* external);
    ~vtkInternal();

    vtkWeakPointer<vtkMRMLSelectionNode> SelectionNode;

    vtkNew<vtkRenderer> RendererOutline;
    vtkNew<vtkRenderStepsPass> BasicPasses;
    vtkNew<vtkOutlineGlowPass> ROIGlowPass;

    vtkNew<vtkPolyData>         CornerROIPolyData;
    vtkNew<vtkPolyDataMapper2D> CornerROIMapper;
    vtkNew<vtkActor2D>          CornerROIActor;

    vtkMRMLFocusRepresentation* External;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal
::vtkInternal(vtkMRMLFocusRepresentation* external)
{
  this->External = external;

  this->ROIGlowPass->SetDelegatePass(this->BasicPasses);
  this->RendererOutline->UseFXAAOn();
  this->RendererOutline->UseShadowsOff();
  this->RendererOutline->UseDepthPeelingOff();
  this->RendererOutline->UseDepthPeelingForVolumesOff();
  this->RendererOutline->SetPass(this->ROIGlowPass);

  this->CornerROIMapper->SetInputData(this->CornerROIPolyData);
  this->CornerROIActor->SetMapper(this->CornerROIMapper);
}

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal::~vtkInternal() = default;

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkMRMLFocusRepresentation);

//----------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkMRMLFocusRepresentation()
{
}

//----------------------------------------------------------------------
vtkMRMLFocusRepresentation::~vtkMRMLFocusRepresentation()
{
  delete this->Internal;
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::GetActors2D(vtkPropCollection * pc)
{
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::ReleaseGraphicsResources(vtkWindow * win)
{
}

//----------------------------------------------------------------------
int vtkMRMLFocusRepresentation::RenderOverlay(vtkViewport * viewport)
{
  int count = 0;
  return count;
}


//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::PrintSelf(ostream & os, vtkIndent indent)
{
  //Superclass typedef defined in vtkTypeMacro() found in vtkSetGet.h
  this->Superclass::PrintSelf(os, indent);
}

//-----------------------------------------------------------------------------
std::string vtkMRMLFocusRepresentation::CanInteract(vtkMRMLInteractionEventData* interactionEventData,
  int& foundComponentType, int& foundComponentIndex, double& closestDistance2, double& handleOpacity)
{
  return "";
}
