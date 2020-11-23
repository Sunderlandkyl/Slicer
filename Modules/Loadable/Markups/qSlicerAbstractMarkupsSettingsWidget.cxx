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

// Qt includes
#include <QDebug>

// QtGUI includes
#include "qSlicerApplication.h"
#include "qSlicerAbstractMarkupsSettingsWidget.h"

// Markups Logic includes
#include <vtkSlicerMarkupsLogic.h>

// Markups MRML includes
#include <vtkMRMLMarkupsNode.h>

// VTK includes
#include <vtkNew.h>
#include <vtkSmartPointer.h>

// --------------------------------------------------------------------------
// qSlicerAbstractMarkupsSettingsWidget methods

// --------------------------------------------------------------------------
qSlicerAbstractMarkupsSettingsWidget::qSlicerAbstractMarkupsSettingsWidget(QWidget* _parent)
  : Superclass(_parent)
{
}

// --------------------------------------------------------------------------
qSlicerAbstractMarkupsSettingsWidget::~qSlicerAbstractMarkupsSettingsWidget() = default;

// --------------------------------------------------------------------------
QStringList qSlicerAbstractMarkupsSettingsWidget::supportedClassNames() const
{
  vtkNew<vtkMRMLMarkupsNode> markupsNode;
  return QStringList() << markupsNode->GetClassName();
}
