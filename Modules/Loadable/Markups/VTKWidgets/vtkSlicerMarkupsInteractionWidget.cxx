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
  Care Ontario, OpenAnatomy, and Brigham and Women’s Hospital through NIH grant R01MH112748.

==============================================================================*/


#include "vtkSlicerMarkupsInteractionWidget.h"
#include "vtkSlicerMarkupsInteractionWidgetRepresentation.h"

#include "vtkMRMLApplicationLogic.h"
#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLInteractionNode.h"
#include "vtkMRMLScene.h"
#include "vtkMRMLSliceCompositeNode.h"
#include "vtkMRMLSliceLogic.h"

// VTK includes
#include <vtkCamera.h>
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkLine.h>
#include <vtkPlane.h>
#include <vtkPointPlacer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderer.h>
#include <vtkTransform.h>
#include <vtkObjectFactory.h>

// MRML includes
#include <vtkMRMLSliceNode.h>

// Markups MRML includes
#include <vtkMRMLMarkupsDisplayNode.h>

//---------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerMarkupsInteractionWidget);

//----------------------------------------------------------------------
vtkSlicerMarkupsInteractionWidget::vtkSlicerMarkupsInteractionWidget()
{
}

//----------------------------------------------------------------------
vtkSlicerMarkupsInteractionWidget::~vtkSlicerMarkupsInteractionWidget() = default;

//----------------------------------------------------------------------
vtkSlicerMarkupsInteractionWidget* vtkSlicerMarkupsInteractionWidget::CreateInstance() const
{
  vtkSlicerMarkupsInteractionWidgetCreateInstanceMacroBody(vtkSlicerMarkupsInteractionWidget);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidget::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidget::GetActiveComponentType()
{
  vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation> rep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  return rep->GetActiveComponentType();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidget::SetActiveComponentType(int type)
{
  vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation> rep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  rep->SetActiveComponentType(type);
}

//----------------------------------------------------------------------
int vtkSlicerMarkupsInteractionWidget::GetActiveComponentIndex()
{
  vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation> rep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  return rep->GetActiveComponentIndex();
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidget::SetActiveComponentIndex(int index)
{
  vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation> rep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->WidgetRep);
  rep->SetActiveComponentIndex(index);
}

//-----------------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double &distance2)
{
  return Superclass::CanProcessInteractionEvent(eventData, distance2);
}


//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidget::ApplyTransform(vtkTransform* transform)
{
  if (!this->GetMarkupsNode())
    {
    return;
    }

  MRMLNodeModifyBlocker blocker(this->GetMarkupsNode());

  // TODO: Handle transformed node
  this->GetMarkupsNode()->ApplyTransform(transform);
}

//----------------------------------------------------------------------
void vtkSlicerMarkupsInteractionWidget::CreateDefaultRepresentation(vtkMRMLMarkupsDisplayNode* displayNode,
  vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation> rep = vtkSmartPointer<vtkSlicerMarkupsInteractionWidgetRepresentation>::New();
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetDisplayNode(displayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//----------------------------------------------------------------------
vtkMRMLMarkupsDisplayNode* vtkSlicerMarkupsInteractionWidget::GetDisplayNode()
{
  vtkSlicerMarkupsInteractionWidgetRepresentation* widgetRep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!widgetRep)
    {
    return nullptr;
    }
  return widgetRep->GetDisplayNode();
}

//----------------------------------------------------------------------
vtkMRMLMarkupsNode* vtkSlicerMarkupsInteractionWidget::GetMarkupsNode()
{
  vtkSlicerMarkupsInteractionWidgetRepresentation* widgetRep = vtkSlicerMarkupsInteractionWidgetRepresentation::SafeDownCast(this->GetRepresentation());
  if (!widgetRep)
  {
    return nullptr;
  }
  return widgetRep->GetMarkupsNode();
}

//-----------------------------------------------------------------------------
bool vtkSlicerMarkupsInteractionWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  bool processedEvent = false;
  processedEvent = Superclass::ProcessInteractionEvent(eventData);
  return processedEvent;
}
