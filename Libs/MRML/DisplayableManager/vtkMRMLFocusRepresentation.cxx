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


#include "vtkMRMLFocusRepresentation.h"

#include "vtkMRMLFocusRepresentationHelper.h"

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

class vtkMRMLFocusRepresentation::vtkInternal
{
  public:
    vtkInternal(vtkMRMLFocusRepresentation* external);
    ~vtkInternal();

    vtkMRMLFocusRepresentation* External;
};

//---------------------------------------------------------------------------
// vtkInternal methods

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal
::vtkInternal(vtkMRMLFocusRepresentation* external)
{
  this->External = external;
}

//---------------------------------------------------------------------------
vtkMRMLFocusRepresentation::vtkInternal::~vtkInternal() = default;

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
  for (std::deque<SliceIntersectionInteractionDisplayPipeline*>::iterator
    sliceIntersectionIt = this->Internal->SliceIntersectionInteractionDisplayPipelines.begin();
    sliceIntersectionIt != this->Internal->SliceIntersectionInteractionDisplayPipelines.end(); ++sliceIntersectionIt)
    {
    (*sliceIntersectionIt)->GetActors2D(pc);
    }
}

//----------------------------------------------------------------------
void vtkMRMLFocusRepresentation::ReleaseGraphicsResources(vtkWindow * win)
{
  for (std::deque<SliceIntersectionInteractionDisplayPipeline*>::iterator
    sliceIntersectionIt = this->Internal->SliceIntersectionInteractionDisplayPipelines.begin();
    sliceIntersectionIt != this->Internal->SliceIntersectionInteractionDisplayPipelines.end(); ++sliceIntersectionIt)
    {
    (*sliceIntersectionIt)->ReleaseGraphicsResources(win);
    }
}

//----------------------------------------------------------------------
int vtkMRMLFocusRepresentation::RenderOverlay(vtkViewport * viewport)
{
  int count = 0;

  for (std::deque<SliceIntersectionInteractionDisplayPipeline*>::iterator
    sliceIntersectionIt = this->Internal->SliceIntersectionInteractionDisplayPipelines.begin();
    sliceIntersectionIt != this->Internal->SliceIntersectionInteractionDisplayPipelines.end(); ++sliceIntersectionIt)
    {
    count += (*sliceIntersectionIt)->RenderOverlay(viewport);
    }
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
