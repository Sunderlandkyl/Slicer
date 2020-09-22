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

#include "vtkSlicerROIWidget.h"

#include "vtkMRMLInteractionEventData.h"
#include "vtkMRMLMarkupsROINode.h"
#include "vtkMRMLSliceNode.h"
#include "vtkSlicerROIRepresentation2D.h"
#include "vtkSlicerROIRepresentation3D.h"

// VTK includes
#include <vtkCommand.h>
#include <vtkEvent.h>
#include <vtkPointPlacer.h>
#include <vtkTransform.h>

vtkStandardNewMacro(vtkSlicerROIWidget);

//----------------------------------------------------------------------
vtkSlicerROIWidget::vtkSlicerROIWidget()
{
  this->SetEventTranslationClickAndDrag(WidgetStateOnWidget, vtkCommand::LeftButtonPressEvent, vtkEvent::ShiftModifier,
    WidgetStateTranslatePlane, WidgetEventPlaneMoveStart, WidgetEventPlaneMoveEnd);
}

//----------------------------------------------------------------------
vtkSlicerROIWidget::~vtkSlicerROIWidget() = default;

//----------------------------------------------------------------------
void vtkSlicerROIWidget::CreateDefaultRepresentation(
  vtkMRMLMarkupsDisplayNode* markupsDisplayNode, vtkMRMLAbstractViewNode* viewNode, vtkRenderer* renderer)
{
  vtkSmartPointer<vtkSlicerMarkupsWidgetRepresentation> rep = nullptr;
  if (vtkMRMLSliceNode::SafeDownCast(viewNode))
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation2D>::New();
    }
  else
    {
    rep = vtkSmartPointer<vtkSlicerROIRepresentation3D>::New();
    }
  this->SetRenderer(renderer);
  this->SetRepresentation(rep);
  rep->SetViewNode(viewNode);
  rep->SetMarkupsDisplayNode(markupsDisplayNode);
  rep->UpdateFromMRML(nullptr, 0); // full update
}

//-----------------------------------------------------------------------------
bool vtkSlicerROIWidget::CanProcessInteractionEvent(vtkMRMLInteractionEventData* eventData, double& distance2)
{
  vtkSlicerMarkupsWidgetRepresentation* rep = this->GetMarkupsRepresentation();
  if (!rep)
    {
    return false;
    }
  if (this->WidgetState == WidgetStateTranslatePlane)
    {
    distance2 = 0.0;
    return true;
    }
  else if (eventData->GetType() == vtkCommand::LeftButtonPressEvent &&
           !(eventData->GetModifiers() & vtkEvent::ShiftModifier))
    {
    // Because the plane widget is so large, we don't want to interrupt the mouse button down event if it is not neccesary,
    // because we would interfere with the camera rotation function.
    // If the shift modifier is not enabled, then return false when the active component is not a plane.
    vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
    if (displayNode && displayNode->GetActiveComponentType() == vtkMRMLMarkupsDisplayNode::ComponentPlane)
      {
      return false;
      }
    }
  return Superclass::CanProcessInteractionEvent(eventData, distance2);
}

//-----------------------------------------------------------------------------
bool vtkSlicerROIWidget::ProcessInteractionEvent(vtkMRMLInteractionEventData* eventData)
{
  unsigned long widgetEvent = this->TranslateInteractionEventToWidgetEvent(eventData);

  bool processedEvent = false;
  switch (widgetEvent)
  {
  case WidgetEventPlaneMoveStart:
    processedEvent = this->ProcessPlaneMoveStart(eventData);
    break;
  case WidgetEventMouseMove:
    processedEvent = this->ProcessMouseMove(eventData);
    break;
  case WidgetEventPlaneMoveEnd:
    processedEvent = this->ProcessPlaneMoveEnd(eventData);
    break;
  }

  if (!processedEvent)
    {
    processedEvent = Superclass::ProcessInteractionEvent(eventData);
    }

  return processedEvent;
}

//----------------------------------------------------------------------
bool vtkSlicerROIWidget::ProcessPlaneMoveStart(vtkMRMLInteractionEventData* eventData)
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || displayNode->GetActiveComponentType() != vtkMRMLMarkupsDisplayNode::ComponentPlane)
    {
    return false;
    }
  this->SetWidgetState(WidgetStateTranslatePlane);
  this->StartWidgetInteraction(eventData);
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerROIWidget::ProcessPlaneMoveEnd(vtkMRMLInteractionEventData* vtkNotUsed(eventData))
{
  vtkMRMLMarkupsDisplayNode* displayNode = this->GetMarkupsDisplayNode();
  if (!displayNode || displayNode->GetActiveComponentType() != vtkMRMLMarkupsDisplayNode::ComponentPlane)
    {
    return false;
    }
  this->SetWidgetState(WidgetStateOnWidget);
  this->EndWidgetInteraction();
  return true;
}

//----------------------------------------------------------------------
bool vtkSlicerROIWidget::ProcessMouseMove(vtkMRMLInteractionEventData* eventData)
{
  return Superclass::ProcessMouseMove(eventData);
}
